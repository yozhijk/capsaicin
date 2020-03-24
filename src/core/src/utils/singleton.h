#pragma once

namespace capsaicin
{
template <typename T>
struct Singleton
{
    static T& instance()
    {
        static T t;
        return t;
    }
};
}  // namespace capsaicin