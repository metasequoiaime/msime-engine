#include "rescore_worker.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace neural
{
namespace
{
// 结果表只是给「同一次输入被重查一遍」兜底的，不是长期缓存：装满就整张丢掉，比维护 LRU 简单，
// 代价也只是重算一次。
constexpr std::size_t kMaxResults = 128;
constexpr std::size_t kWorkerCount = 2;

std::string make_key(const SentenceModel &model, const std::string &context, const std::vector<std::string> &sentences)
{
    // \x01 不会出现在 UTF-8 文本里，拿它当分隔符就不会把两批不同的候选拼成同一个键。
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(&model));
    key.push_back('\x01');
    key += context;
    for (const std::string &sentence : sentences)
    {
        key.push_back('\x01');
        key += sentence;
    }
    return key;
}
} // namespace

RescoreWorker &RescoreWorker::instance()
{
    // 故意泄漏：后台线程只碰这个对象的成员，而静态对象的析构发生在退出时线程可能已被系统干掉之
    // 后，join 会挂、detach 会悬空。永不析构把这道选择题去掉，进程退出时操作系统收走就是了。
    static RescoreWorker *worker = new RescoreWorker();
    return *worker;
}

void RescoreWorker::set_ready_callback(Ready ready)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ready_ = std::move(ready);
}

void RescoreWorker::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    done_.clear();
    pending_.clear();
}

std::optional<std::vector<std::size_t>> RescoreWorker::order_for(const SentenceModel &model, const std::string &context,
                                                                 const std::vector<std::string> &sentences,
                                                                 const std::vector<double> &static_scores,
                                                                 const RerankOptions &options)
{
    std::string key = make_key(model, context, sentences);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
        {
            return std::nullopt;
        }
        const auto found = done_.find(key);
        if (found != done_.end())
        {
            return found->second;
        }
        Job job;
        job.model = &model;
        job.key = std::move(key);
        job.context = context;
        job.sentences = sentences;
        job.static_scores = static_scores;
        job.options = options;
        pending_[&model] = std::move(job);
        ensure_threads();
    }
    wake_.notify_one();
    return std::nullopt;
}

void RescoreWorker::shutdown()
{
    std::unique_lock<std::mutex> lock(mutex_);
    stopping_ = true;
    ready_ = nullptr; // 目标很可能已经析构了，这之后一律不再回调
    pending_.clear(); // 排着队还没开始的，直接不要了
    wake_.notify_all();
    idle_.wait(lock, [this] { return busy_workers_ == 0; });
}

void RescoreWorker::ensure_threads()
{
    // 调用方持有 mutex_。两个执行槽让速度优先模型不必等效果优先模型打完分。
    // 线程只在第一次真有活干时才起，没开神经重排的用户不会平白多线程。
    if (threads_started_)
    {
        return;
    }
    threads_started_ = true;
    for (std::size_t i = 0; i < kWorkerCount; ++i)
        std::thread([this] { run(); }).detach();
    std::atexit([] { instance().shutdown(); });
}

void RescoreWorker::run()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {
                return stopping_ || std::any_of(pending_.begin(), pending_.end(), [this](const auto &item) {
                           return active_models_.find(item.first) == active_models_.end();
                       });
            });
            if (stopping_)
            {
                return;
            }
            auto next = std::find_if(pending_.begin(), pending_.end(), [this](const auto &item) {
                return active_models_.find(item.first) == active_models_.end();
            });
            job = std::move(next->second);
            pending_.erase(next);
            active_models_.insert(job.model);
            ++busy_workers_;
        }

        // 打分在锁外做：它是这里最慢的一步，占着锁会让按键路径上的 order_for 跟着等。
        std::vector<std::size_t> order =
            rerank_order(*job.model, job.context, job.sentences, job.static_scores, job.options);

        Ready ready;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 期间可能又来了新请求，说明用户还在打字，这一份存下来留给「他退回来」的情况。
            if (done_.size() >= kMaxResults)
            {
                done_.clear();
            }
            done_.emplace(std::move(job.key), std::move(order));
            ready = ready_; // shutdown 已经清过就是空的，那就不叫了
        }

        // 回调在锁外发：它会一路走到重查，重查又会调 order_for，在锁内发就是自锁。busy_ 要压到回
        // 调之后才放，否则 shutdown 会在回调正往已析构的目标里走的时候就返回。
        if (ready)
        {
            ready();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_models_.erase(job.model);
            --busy_workers_;
        }
        wake_.notify_all();
        idle_.notify_all();
    }
}

} // namespace neural
