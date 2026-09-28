#pragma once

#include "../core/query_request.h"
#include "../core/scheme_type.h"
#include <string>

class IInputScheme
{
  public:
    virtual ~IInputScheme() = default;

    virtual void reset() = 0;
    virtual void handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch) = 0;
    virtual QueryRequest build_request() const = 0;
    virtual std::string get_preedit() const = 0;
    virtual SchemeType type() const = 0;
    virtual void set_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases) = 0;
};
