#pragma once

#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

template <typename Key, typename Value> class CircularBuffer
{
  public:
    explicit CircularBuffer(size_t capacity) : _capacity(capacity)
    {
    }

    void insert(const Key &key, const Value &value)
    {
        auto it = _map.find(key);

        if (it != _map.end())
        {
            it->second.first = value;
            return;
        }

        if (_list.size() >= _capacity)
        {
            const Key &oldest_key = _list.front();
            _map.erase(oldest_key);
            _list.pop_front();
        }

        _list.push_back(key);
        _map[key] = {value, std::prev(_list.end())};
    }

    std::optional<Value> get(const Key &key) const
    {
        auto it = _map.find(key);
        if (it != _map.end())
        {
            return it->second.first;
        }
        return std::nullopt;
    }

    template <typename Function> bool update_if_cached(const Key &key, Function function)
    {
        auto cached = get(key);
        if (!cached)
            return false;

        auto value = std::move(*cached);
        if (!function(value))
            return false;

        insert(key, value);
        return true;
    }

    // Presence test for callers that only need to know whether a key is cached: get() returns the value by value, so
    // using it as a probe deep-copies the whole entry and throws it away.
    bool contains(const Key &key) const
    {
        return _map.find(key) != _map.end();
    }

    bool remove(const Key &key)
    {
        auto it = _map.find(key);
        if (it == _map.end())
            return false;

        _list.erase(it->second.second);
        _map.erase(it);
        return true;
    }

    void clear()
    {
        _map.clear();
        _list.clear();
    }

    size_t size() const
    {
        return _map.size();
    }

    bool empty() const
    {
        return _map.empty();
    }

  private:
    size_t _capacity;
    std::list<Key> _list;
    std::unordered_map<Key, std::pair<Value, typename std::list<Key>::iterator>> _map;
};
