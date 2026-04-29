//
// JsonValue.hpp
// Author: Antoine Bastide
// Date: 05.07.2025
//

#ifndef JSON_HPP
#define JSON_HPP

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Concepts.hpp"

class JSON;

/// Representation of a JSON null value
struct null_t final {
  constexpr null_t() = default;
  explicit constexpr null_t(int) {}

  constexpr bool operator==(const null_t &) const {
    return true;
  }

  constexpr bool operator!=(const null_t &) const {
    return false;
  }
};

static inline constexpr null_t null{0};

class JSON final {
  friend class JSONParser;

  enum JSONType {
    Null = 0, Boolean, Number, String, array, object
  };
  public:
    using JSONArray = std::vector<JSON>;
    using JSONObject = std::unordered_map<std::string, JSON>;

    /// Creates a JSON null value
    JSON();
    /// Creates a JSON boolean value
    explicit JSON(bool value);
    explicit JSON(const char *value);
    /// Assigns the given array or object to this instance.
    JSON(const std::initializer_list<JSON> &values);

    /// Assigns the given arithmetical typed value to this instance.
    template<IsNumber T> explicit JSON(const T &value) {
      type = Number;
      data = static_cast<double>(value);
    }

    /// Assigns the given string typed value to this instance.
    template<IsString T> explicit JSON(const T &value) {
      type = String;
      if constexpr (std::is_same_v<std::decay_t<T>, char>)
        data = std::string(1, value);
      else if constexpr (std::is_same_v<std::decay_t<T>, const char *>)
        data = std::string(value);
      else
        data = static_cast<std::string>(value);
    }

    /// Assigns the given container typed value to this instance.
    template<IsContainer T> requires std::is_convertible_v<typename T::value_type, JSON>
    explicit JSON(const T &values) {
      auto arr = JSONArray();
      arr.reserve(std::distance(std::begin(values), std::end(values)));
      arr.resize(std::distance(std::begin(values), std::end(values)));
      for (const auto &v: values)
        arr.emplace_back(v);

      type = array;
      data = arr;
    }

    /// Assigns the given map typed value to this instance.
    template<IsMap T> requires
      std::is_convertible_v<typename T::key_type, std::string> && std::is_convertible_v<typename T::mapped_type, JSON>
    explicit JSON(const T &values) {
      auto obj = JSONObject();
      obj.reserve(std::distance(std::begin(values), std::end(values)));
      for (const auto &[k, v]: values) {
        if constexpr (std::is_same_v<typename T::key_type, std::string>)
          obj.emplace(k, v);
        else
          obj.emplace(static_cast<std::string>(k), v);
      }

      type = object;
      data = obj;
    }

    /// Assigns the given tuple value to this instance.
    template<typename... Ts> requires ((std::is_constructible_v<JSON, Ts> && ...))
    explicit JSON(const std::tuple<Ts...> &tuple) {
      auto arr = JSONArray();
      arr.reserve(sizeof...(Ts));
      arr.resize(sizeof...(Ts));
      std::apply(
        [&](const Ts &... elems) {
          (arr.emplace_back(elems), ...);
        }, tuple
      );

      type = array;
      data = arr;
    }

    bool operator==(const JSON &other) const;
    bool operator!=(const JSON &other) const;

    bool operator<(const JSON &other) const;
    bool operator<=(const JSON &other) const;
    bool operator>(const JSON &other) const;
    bool operator>=(const JSON &other) const;

    /// Dump's this instance in the given ostream
    friend std::ostream &operator<<(std::ostream &os, const JSON &value);

    /// Load's this instance from the given istream
    friend std::istream &operator>>(std::istream &is, JSON &value);

    /// @returns A reference to the JSON element at the specified index.
    /// @param index The position of the element to access within the array.
    /// @note If the current instance is not an array, it is converted into one. If the index is out of bounds, the array is resized to accommodate it.
    JSON &operator[](size_t index);

    /// @returns A const reference to the JSON element at the specified index.
    /// @param index The position of the element to access within the array.
    /// @note If the current instance is not an array, it is converted into one. If the index is out of bounds, the array is resized to accommodate it.
    const JSON &operator[](size_t index) const;

    /// @returns A reference to the JSON element at the specified key.
    /// @param key The key of the element to access within the object.
    /// @note If the current instance is not an object, it is converted into one.
    JSON &operator[](const std::string &key);

    /// @returns A const reference to the JSON element at the specified key.
    /// @param key The key of the element to access within the object.
    /// @note If the current instance is not an object, it is converted into one.
    const JSON &operator[](const std::string &key) const;

    /// @returns A reference to the stored null value
    /// @throws std::logic_error if called on a non-null type
    [[nodiscard]] null_t &GetNull();

    /// @returns A reference to the stored boolean value
    /// @throws std::logic_error if called on a non-boolean type
    [[nodiscard]] bool &GetBool();

    /// @returns A reference to the stored number value
    /// @throws std::logic_error if called on a non-number type
    [[nodiscard]] double &GetNumber();

    /// @returns A reference to the stored string value
    /// @throws std::logic_error if called on a non-string type
    [[nodiscard]] std::string &GetString();

    /// @returns A reference to the stored array value
    /// @throws std::logic_error if called on a non-array type
    [[nodiscard]] JSONArray &GetArray();

    /// @returns A reference to the stored object value
    /// @throws std::logic_error if called on a non-object type
    [[nodiscard]] JSONObject &GetObject();

    /// @returns A constant reference to the stored null value
    /// @throws std::logic_error if called on a non-null type
    [[nodiscard]] const null_t &GetNull() const;

    /// @returns A constant reference to the stored boolean value
    /// @throws std::logic_error if called on a non-boolean type
    [[nodiscard]] const bool &GetBool() const;

    /// @returns A constant reference to the stored number value
    /// @throws std::logic_error if called on a non-number type
    [[nodiscard]] const double &GetNumber() const;

    /// @returns A constant reference to the stored string value
    /// @throws std::logic_error if called on a non-string type
    [[nodiscard]] const std::string &GetString() const;

    /// @returns A constant reference to the stored array value
    /// @throws std::logic_error if called on a non-array type
    [[nodiscard]] const JSONArray &GetArray() const;

    /// @returns A constant reference to the stored object value
    /// @throws std::logic_error if called on a non-object type
    [[nodiscard]] const JSONObject &GetObject() const;

    /// Appends a JSON value to this instance.
    /// @param value The JSON value to add.
    /// @note If this JSON is not of type array, it will be implicitly converted to a JSON array.
    void PushBack(const JSON &value);

    /// Appends multiple JSON values to this instance.
    /// @param values The JSON values to add.
    /// @note If this JSON is not of type array, it will be implicitly converted to a JSON array.
    void PushBackAll(const std::initializer_list<JSON> &values);

    /// Inserts a JSON value into this array at the specified index.
    /// @param index The position in the array where the value will be inserted.
    /// @param value The JSON value to insert
    /// @throws std::out_of_range if index exceeds the current array size.
    /// @throws std::logic_error if this JSON is not of type array.
    void Insert(size_t index, const JSON &value);

    /// Removes the JSON value at the given index from this instance.
    /// @param index The index at which to erase the JSON value.
    /// @throws std::out_of_range if index exceeds the current array size.
    /// @throws std::logic_error if this JSON is not of type array.
    void Erase(size_t index);

    /// Resizes this instance to the given size.
    /// @param size The new size of this instance.
    /// @note If this JSON is not of type array, it will be implicitly converted to a JSON array.
    void Resize(size_t size);

    /// Reserves the given number of elements in memory for this instance
    /// @param size The number of elements to reserve of this instance.
    /// @throws std::logic_error if this JSON if not of type array or object
    void Reserve(size_t size);

    /// Reserves and resizes this instance to the given size.
    /// @param size The new size and number of elements of this instance.
    /// @note If this JSON is not of type array, it will be implicitly converted to a JSON array.
    void ReserveAndResize(size_t size);

    /// @return A reference to the first element of this instance.
    /// @throws std::logic_error if this JSON is not of type array.
    JSON &Front();

    /// @return A const reference to the first element of this instance.
    /// @throws std::logic_error if this JSON is not of type array.
    [[nodiscard]] const JSON &Front() const;

    /// @return A reference to the last element of this instance.
    /// @throws std::logic_error if this JSON is not of type array.
    JSON &Back();

    /// @return A const reference to the last element of this instance.
    /// @throws std::logic_error if this JSON is not of type array.
    [[nodiscard]] const JSON &Back() const;

    /// @returns A reference to the element in this instance at the specified index.
    /// @param index The position of the element within the instance.
    /// @throws std::out_of_range if index exceeds the current array size.
    /// @throws std::logic_error if this JSON is not of type array.
    [[nodiscard]] JSON &At(size_t index);

    /// @returns A const reference to the element in this instance at the specified index.
    /// @param index The position of the element within the instance.
    /// @throws std::out_of_range if index exceeds the current array size.
    /// @throws std::logic_error if this JSON is not of type array.
    [[nodiscard]] const JSON &At(size_t index) const;

    /// @returns A reference to the element in this instance at the specified index or,
    /// the default value if it is null or if this instance isn't an array.
    /// @param index The position of the element within the instance.
    /// @param defaultValue The default value to return in case of errors.
    [[nodiscard]] JSON &Value(size_t index, JSON &defaultValue);

    /// @returns A const reference to the element in this instance at the specified index or,
    /// the default value if it is null or if this instance isn't an array.
    /// @param index The position of the element within the instance.
    /// @param defaultValue The default value to return in case of errors.
    [[nodiscard]] const JSON &Value(size_t index, JSON &defaultValue) const;

    /// @returns true if this instance contains the given JSON value, false if not.
    /// @param value The value to check the existence of.
    /// @throws std::logic_error if this JSON is not of type array or object.
    [[nodiscard]] bool Contains(const JSON &value) const;

    /// Assigns the specified value to the specified key in this instance.
    /// @note If this JSON is not of type object, it will be implicitly converted to a JSON object.
    /// @param key The key at which the value will be stored.
    /// @param value The values to store.
    void Emplace(const std::string &key, const JSON &value);

    /// Assigns the specified values to their specified keys in this instance.
    /// @note If this JSON is not of type object, it will be implicitly converted to a JSON object.
    /// @param pairs The pairs of key-value to add to this instance.
    void EmplaceAll(const std::initializer_list<std::pair<std::string, JSON>> &pairs);

    /// Removes the JSON value assigned to the given key from this instance.
    /// @param key The key at which to erase the JSON value.
    /// @throws std::out_of_range if index exceeds the current array size.
    /// @throws std::logic_error if this JSON is not of type array.
    void Erase(const std::string &key);

    /// Removes the JSON values assigned to the given keys from this instance.
    /// @param keys The keys at which to erase the JSON value.
    /// @throws std::out_of_range if index exceeds the current array size.
    /// @throws std::logic_error if this JSON is not of type array.
    void EraseAll(const std::initializer_list<std::string> &keys);

    /// @returns A reference to the element in this instance at the specified key.
    /// @param key The key of the element within the instance.
    /// @throws std::out_of_range if key does not exist in the current object.
    /// @throws std::logic_error if this JSON is not of type array.
    [[nodiscard]] JSON &At(const std::string &key);

    /// @returns A const reference to the element in this instance at the specified key.
    /// @param key The key of the element within the instance.
    /// @throws std::out_of_range if key does not exist in the current object.
    /// @throws std::logic_error if this JSON is not of type array.
    [[nodiscard]] const JSON &At(const std::string &key) const;

    /// @returns A reference to the element in this instance at the specified key or,
    /// the default value if it is null or if this instance isn't an array.
    /// @param key The key of the element within the instance.
    /// @param defaultValue The default value to return in case of errors.
    [[nodiscard]] JSON &Value(const std::string &key, JSON &defaultValue);

    /// @returns A const reference to the element in this instance at the specified key or,
    /// the default value if it is null or if this instance isn't an array.
    /// @param key The key of the element within the instance.
    /// @param defaultValue The default value to return in case of errors.
    [[nodiscard]] const JSON &Value(const std::string &key, JSON &defaultValue) const;

    /// @returns true if this instance contains the given key, false if not.
    /// @param key The key to check the existence of.
    /// @throws std::logic_error if this JSON is not of type object.
    [[nodiscard]] bool Contains(const std::string &key) const;

    /// @returns true if this instance contains the given key, false if not.
    /// @param key The key to check the existence of.
    /// @throws std::logic_error if this JSON is not of type object.
    [[nodiscard]] bool Contains(const char *key) const;

    /// @returns The size of this instance.
    /// @throws std::logic_error if this JSON is not of type array or object.
    [[nodiscard]] size_t Size() const;

    /// @returns true if this instance is empty, false if not.
    /// @throws std::logic_error if this JSON is not of type array or object.
    [[nodiscard]] bool Empty() const;

    /// Clears all the values contained in this instance.
    void Clear();

    /// @returns true if this instance in a JSON null value.
    [[nodiscard]] bool IsNull() const;

    /// @returns true if this instance in a JSON boolean value.
    [[nodiscard]] bool IsBool() const;

    /// @returns true if this instance in a JSON number.
    [[nodiscard]] bool IsNumber() const;

    /// @returns true if this instance in a JSON string.
    [[nodiscard]] bool IsString() const;

    /// @returns true if this instance in a JSON array.
    [[nodiscard]] bool IsArray() const;

    /// @returns true if this instance in a JSON object.
    [[nodiscard]] bool IsObject() const;

    /// @returns A stringified representation of this instance
    /// @param prettyPrint Whether to pretty print the json or not
    /// @param indentChar Character used for indentation
    [[nodiscard]] std::string Dump(bool prettyPrint = false, char indentChar = ' ') const;

    /// @returns A new JSON array containing the given values
    /// @param values The optional values to add to this array
    [[nodiscard]] static JSON Array(const std::initializer_list<JSON> &values = {});

    /// @returns A new JSON object containing the given keys and values
    /// @param values The optional keys and values to add to this array
    [[nodiscard]] static JSON Object(const std::initializer_list<std::pair<std::string, JSON>> &values = {});

    static constexpr std::string_view TypeToString(const JSON &json) {
      switch (json.type) {
        case Null:
          return "null";
        case Boolean:
          return "boolean";
        case Number:
          return "number";
        case String:
          return "string";
        case array:
          return "array";
        case object:
          return "object";
      }
      return "unknown";
    }
  private:
    JSONType type;
    std::variant<null_t, bool, double, std::string, JSONArray, JSONObject> data;

    /// @returns true if the instance is an array or an object, false if not
    [[nodiscard]] bool isComplexType() const;

    template<typename T> [[nodiscard]] bool typeMatches() const {
      if constexpr (std::is_same_v<std::remove_cvref_t<T>, bool>)
        return type == Boolean;
      if constexpr (std::is_same_v<std::remove_cvref_t<T>, double>)
        return type == Number;
      if constexpr (std::is_same_v<std::remove_cvref_t<T>, std::string>)
        return type == String;
      if constexpr (std::is_same_v<std::remove_cvref_t<T>, JSONArray>)
        return type == array;
      if constexpr (std::is_same_v<std::remove_cvref_t<T>, JSONObject>)
        return type == object;
      if constexpr (std::is_same_v<std::remove_cvref_t<T>, null_t>)
        return type == Null;
      return false;
    }

    [[nodiscard]] std::string dump(bool prettyPrint, int indentSize = -1, char indentChar = ' ') const;
};

#endif //JSON_HPP
