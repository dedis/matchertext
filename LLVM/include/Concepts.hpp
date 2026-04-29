//
// Concepts.hpp
// Author: Antoine Bastide
// Date: 18.06.2025
//

#ifndef CONCEPTS_HPP
#define CONCEPTS_HPP

template<typename T> concept IsNonOwningString =
    std::is_same_v<std::remove_cvref_t<T>, std::string_view> ||
    std::is_same_v<std::remove_cvref_t<T>, const char *> ||
    std::is_same_v<std::remove_cvref_t<T>, char *>;

template<typename T> concept IsString = std::is_same_v<std::remove_cvref_t<T>, std::string> || IsNonOwningString<T>;

template<typename T> concept IsNumber =
    std::is_arithmetic_v<std::remove_cvref_t<T>> &&
    !std::is_same_v<std::remove_cvref_t<T>, bool> &&
    !IsString<T>;

template<typename T> concept IsPair =
    requires {
      typename std::remove_cvref_t<T>;
    } && std::is_same_v<
      std::remove_cvref_t<T>,
      std::pair<typename std::remove_cvref_t<T>::first_type, typename std::remove_cvref_t<T>::second_type>
    >;

template<typename T> concept IsMap =
    requires {
      typename T::value_type;
      typename T::key_type;
      typename T::mapped_type;
      std::begin(std::declval<T &>());
      std::end(std::declval<T &>());
    } && IsPair<std::remove_cvref_t<typename T::value_type>>;

template<typename T> concept IsContainer =
    requires {
      typename T::value_type;
      std::begin(std::declval<T &>());
      std::end(std::declval<T &>());
    } && !IsMap<T> && !IsString<T>;

#endif //CONCEPTS_HPP
