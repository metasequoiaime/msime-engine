#pragma once

#include "input_scheme.h"
#include <string>
#include <vector>

class JapaneseRomajiScheme : public IInputScheme
{
  public:
    void reset() override;
    void handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch) override;
    QueryRequest build_request() const override;
    std::string get_preedit() const override;
    SchemeType type() const override;
    void set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases) override;
    /// 把刚打的那个假名换成它的下一个变体(小書き/濁点/半濁点),回到原样后再循环。
    /// Returns false when there is no completed kana to modify, which is what an empty composition
    /// or a half-finished romaji tail like "k" looks like.
    bool cycle_last_kana_variant();

  private:
    std::string raw_input_;
    std::vector<KeyStroke> key_strokes_;
};
