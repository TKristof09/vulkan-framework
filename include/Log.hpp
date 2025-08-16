#pragma once

#include <print>
namespace Log
{

enum class LogLevel
{
    INFO,
    WARN,
    ERROR
};
template<typename... Args>
void Log(LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
    constexpr std::string_view RESET  = "\033[0m";
    constexpr std::string_view GREEN  = "\033[32m";
    constexpr std::string_view YELLOW = "\033[33m";
    constexpr std::string_view RED    = "\033[31m";

    std::string formatted_output;

    switch(level)
    {
    case LogLevel::INFO:
        formatted_output += GREEN;
        formatted_output += "[INFO] ";
        break;
    case LogLevel::WARN:
        formatted_output += YELLOW;
        formatted_output += "[WARN] ";
        break;
    case LogLevel::ERROR:
        formatted_output += RED;
        formatted_output += "[ERROR] ";
        break;
    }

    formatted_output += std::format(fmt, std::forward<Args>(args)...);
    formatted_output += RESET;
    formatted_output += "\n";

    std::print("{}", formatted_output);
}

template<typename... Args>
void Info(std::format_string<Args...> fmt, Args&&... args)
{
    Log(LogLevel::INFO, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args)
{
    Log(LogLevel::WARN, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args)
{
    Log(LogLevel::ERROR, fmt, std::forward<Args>(args)...);
}
}
