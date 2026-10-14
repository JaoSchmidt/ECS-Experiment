#pragma once
template <typename... Ts>
concept IsNoneType = (sizeof...(Ts) == 0);

template <typename... Ts>
concept IsSingleType = (sizeof...(Ts) == 1);

template <typename... Ts>
concept IsMultipleTypes = (sizeof...(Ts) > 1);
