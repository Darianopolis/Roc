#pragma once

#include "types.hpp"

struct CommandElement
{
    std::string_view key;
    std::string_view value;
    bool force_positional;
    bool has_value;

    template<typename T>
    auto value_as() -> std::optional<T>
    {
        if (!has_value) return std::nullopt;

        T value;
        auto res = std::from_chars(value.begin(), value.end(), value);
        if (!res) return std::nullopt;

        return value;
    }
};

struct CommandArgs
{
    std::string_view named;
    std::vector<CommandElement> elements;

    CommandArgs(int argc, char* argv[])
    {
        if (argc == 0) return;
        named = argv[0];
        for (int i = 1; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--") {
                // Interpret remaining args as positionals
                for (int j = i + 1; j < argc; ++j) {
                    elements.emplace_back(CommandElement {
                        .value = argv[j],
                        .force_positional = true,
                        .has_value = true,
                    });
                }
                break;
            } if (arg.starts_with("-")) {
                size_t equal = arg.find_first_of('=');
                if (equal != std::string_view::npos) {
                    // --key=value
                    elements.emplace_back(CommandElement {
                        .key = arg.substr(0, equal),
                        .value = arg.substr(equal + 1),
                        .has_value = true,
                    });
                } else {
                    // --key
                    elements.emplace_back(CommandElement {
                        .key = arg,
                        .has_value = false,
                    });
                }
            } else {
                elements.emplace_back(CommandElement {
                    .value = argv[i],
                    .has_value = true,
                });
            }
        }
    }

    auto find(std::string_view key) const -> const CommandElement*
    {
        for (auto& element : elements) {
            if (element.key == key) return &element;
        }
        return nullptr;
    }
};
