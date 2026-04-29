//
// JsonValue.cpp
// Author: Antoine Bastide
// Date: 05.07.2025
//

#include <cmath>
#include <sstream>

#include "JSON.hpp"

#include <iostream>

#include "JsonParser.hpp"

JSON::JSON()
  : type(Null), data(null) {}

JSON::JSON(const bool value)
  : type(Boolean), data(value) {}

JSON::JSON(const std::initializer_list<JSON> &values) {
  const bool isObject = std::ranges::all_of(
    values, [](const JSON &j) {
      return j.IsArray() && j.Size() == 2 && j[0].IsString();
    }
  );

  type = isObject ? object : array;

  if (isObject) {
    JSONObject obj;
    for (const auto &pair: values) {
      obj.emplace(pair[0].GetString(), pair[1]);
    }
    data = std::move(obj);
  } else
    data = values;
}

bool JSON::operator==(const JSON &other) const {
  return type == other.type && data == other.data;
}

bool JSON::operator!=(const JSON &other) const {
  return !(*this == other);
}

bool JSON::operator<(const JSON &other) const {
  return type < other.type;
}

bool JSON::operator<=(const JSON &other) const {
  return type <= other.type;
}

bool JSON::operator>(const JSON &other) const {
  return type > other.type;
}

bool JSON::operator>=(const JSON &other) const {
  return type >= other.type;
}

std::ostream &operator<<(std::ostream &os, const JSON &value) {
  return os << value.Dump();
}

std::istream &operator>>(std::istream &is, JSON &value) {
  value = JSONParser(is).Parse();
  return is;
}

JSON &JSON::operator[](const size_t index) {
  if (type != array) {
    type = array;
    data = JSONArray{};
  }

  auto &arr = std::get<JSONArray>(data);
  if (index >= arr.size())
    arr.resize(index + 1);

  return arr[index];
}

const JSON &JSON::operator[](const size_t index) const {
  if (type == array) {
    if (const auto &array = GetArray(); index < array.size())
      return array[index];
    std::cout << "ERROR: JSON::operator[]\n";
    throw std::out_of_range("");
  }
  std::cout << "ERROR: JSON::operator[] on a non array-type\n";
  throw std::logic_error("");
}

JSON &JSON::operator[](const std::string &key) {
  if (type != object) {
    type = object;
    data = JSONObject{};
  }
  auto &obj = std::get<JSONObject>(data);
  return obj[key];
}

const JSON &JSON::operator[](const std::string &key) const {
  if (type == object) {
    if (const auto &obj = GetObject(); obj.contains(key))
      return obj.at(key);
    std::cout << "ERROR: JSON::operator[] key: \"" + key + "\" not found\n";
    std::cout << "ERROR: JSON::operator[] key: \"" + key + "\" not found\n";
    throw std::out_of_range("");
  }
  std::cout << "ERROR: JSON::operator[] on a non object-type\n";
  throw std::logic_error("");
}

null_t &JSON::GetNull() {
  if (type == Null)
    return std::get<null_t>(data);
  std::cout << "ERROR: JSON::GetNull called on a non-null type\n";
  throw std::logic_error("");
}

bool &JSON::GetBool() {
  if (type == Boolean)
    return std::get<bool>(data);
  std::cout << "ERROR: JSON::GetBool called on a non-boolean type\n";
  throw std::logic_error("");
}

double &JSON::GetNumber() {
  if (type == Number)
    return std::get<double>(data);

  std::cout << "ERROR: JSON::GetNumber called on a non-number type\n";
  throw std::logic_error("");
}

std::string &JSON::GetString() {
  if (type == String)
    return std::get<std::string>(data);
  std::cout << "ERROR: JSON::GetString called on a non-string type\n";
  throw std::logic_error("");
}

JSON::JSONArray &JSON::GetArray() {
  if (type == array)
    return std::get<JSONArray>(data);
  std::cout << "ERROR: JSON::GetArray called on a non-array type\n";
  throw std::logic_error("");
}

JSON::JSONObject &JSON::GetObject() {
  if (type == object)
    return std::get<JSONObject>(data);
  std::cout << "ERROR: JSON::GetObject called on a non-object type\n";
  throw std::logic_error("");
}

const null_t &JSON::GetNull() const {
  if (type == Null)
    return std::get<null_t>(data);
  std::cout << "ERROR: JSON::GetNull called on a non-null type\n";
  throw std::logic_error("");
}

const bool &JSON::GetBool() const {
  if (type == Boolean)
    return std::get<bool>(data);
  std::cout << "ERROR: JSON::GetBool called on a non-boolean type\n";
  throw std::logic_error("");
}

const double &JSON::GetNumber() const {
  if (type == Number)
    return std::get<double>(data);
  std::cout << "ERROR: JSON::GetNumber called on a non-number type\n";
  throw std::logic_error("");
}

const std::string &JSON::GetString() const {
  if (type == String)
    return std::get<std::string>(data);
  std::cout << "ERROR: JSON::GetString called on a non-string type\n";
  throw std::logic_error("");
}

const JSON::JSONArray &JSON::GetArray() const {
  if (type == array)
    return std::get<JSONArray>(data);
  std::cout << "ERROR: JSON::GetArray called on a non-array type\n";
  throw std::logic_error("");
}

const JSON::JSONObject &JSON::GetObject() const {
  if (type == object)
    return std::get<JSONObject>(data);
  std::cout << "ERROR: JSON::GetObject called on a non-object type\n";
  throw std::logic_error("");
}

void JSON::PushBack(const JSON &value) {
  if (type != array) {
    type = array;
    data = JSONArray{};
  }
  std::get<JSONArray>(data).push_back(value);
}

void JSON::PushBackAll(const std::initializer_list<JSON> &values) {
  if (type != array) {
    type = array;
    data = JSONArray{};
  }
  auto &array = std::get<JSONArray>(data);
  array.insert(array.end(), values.begin(), values.end());
}

void JSON::Erase(const size_t index) {
  if (type == array) {
    if (auto &array = std::get<JSONArray>(data); index >= array.size()) {
      std::cout << "ERROR: JSON::Erase\n";
      throw std::out_of_range("");
    } else
      std::get<JSONArray>(data).erase(array.begin() + index);
  }
  std::cout << "ERROR: JSON::Erase on a non-array type\n";
  throw std::logic_error("");
}

void JSON::Resize(const size_t size) {
  if (type != array) {
    type = array;
    data = JSONArray{};
  }
  std::get<JSONArray>(data).resize(size);
}

void JSON::Reserve(const size_t size) {
  if (type == array)
    std::get<JSONArray>(data).reserve(size);
  else if (type == object)
    std::get<JSONObject>(data).reserve(size);
  else
    std::cout << "ERROR: JSON::Reserve called on non-array and non-object type\n";
  throw std::logic_error("");
}

void JSON::ReserveAndResize(const size_t size) {
  if (type != array) {
    type = array;
    data = JSONArray{};
  }
  std::get<JSONArray>(data).reserve(size);
  std::get<JSONArray>(data).resize(size);
}

JSON &JSON::Front() {
  if (type == array)
    return std::get<JSONArray>(data).front();
  std::cout << "ERROR: JSON::Front() called on non-array type\n";
  throw std::logic_error("");
}

const JSON &JSON::Front() const {
  if (type == array)
    return std::get<JSONArray>(data).front();
  std::cout << "ERROR: JSON::Front() called on non-array type\n";
  throw std::logic_error("");
}

JSON &JSON::Back() {
  if (type == array)
    return std::get<JSONArray>(data).back();
  std::cout << "ERROR: JSON::Back called on non-array type\n";
  throw std::logic_error("");
}

const JSON &JSON::Back() const {
  if (type == array)
    return std::get<JSONArray>(data).back();
  std::cout << "ERROR: JSON::Back called on non-array type\n";
  throw std::logic_error("");
}

JSON &JSON::At(const size_t index) {
  if (type == array)
    return std::get<JSONArray>(data).at(index);
  std::cout << "ERROR: JSON::At() on a non array-type\n";
  throw std::out_of_range("");
}

const JSON &JSON::At(const size_t index) const {
  if (type == array)
    return std::get<JSONArray>(data).at(index);
  std::cout << "ERROR: JSON::At() on a non array-type\n";
  throw std::out_of_range("");
}

JSON &JSON::Value(const size_t index, JSON &defaultValue) {
  if (type == array) {
    auto &array = GetArray();
    return array[index] == JSON{} ? defaultValue : array[index];
  }
  return defaultValue;
}

const JSON &JSON::Value(const size_t index, JSON &defaultValue) const {
  if (type == array) {
    const auto &array = GetArray();
    return array[index] == JSON{} ? defaultValue : array[index];
  }
  return defaultValue;
}

void JSON::Insert(const size_t index, const JSON &value) {
  if (type == array) {
    if (const auto &array = std::get<JSONArray>(data); index >= array.size()) {
      std::cout << "ERROR: JSON::Insert()\n";
      throw std::out_of_range("");
    }
    std::get<JSONArray>(data).at(index) = value;
  }
  std::cout << "ERROR: JSON::Insert() called on non-array type\n";
  throw std::logic_error("");
}

void JSON::Emplace(const std::string &key, const JSON &value) {
  if (type != object) {
    type = object;
    data = JSONObject{};
  }
  std::get<JSONObject>(data)[key] = value;
}

void JSON::EmplaceAll(const std::initializer_list<std::pair<std::string, JSON>> &pairs) {
  if (type != object) {
    type = object;
    data = JSONObject{};
  }
  std::get<JSONObject>(data).insert(pairs.begin(), pairs.end());
}

void JSON::Erase(const std::string &key) {
  if (type == object)
    std::get<JSONObject>(data).erase(key);
}

void JSON::EraseAll(const std::initializer_list<std::string> &keys) {
  if (type == object)
    for (const auto &key: keys)
      Erase(key);
}

JSON &JSON::At(const std::string &key) {
  if (type == object)
    return std::get<JSONObject>(data).at(key);
  std::cout << "ERROR: JSON::At() on a non object-type\n";
  throw std::out_of_range("");
}

const JSON &JSON::At(const std::string &key) const {
  if (type == object)
    return std::get<JSONObject>(data).at(key);
  std::cout << "ERROR: JSON::At() on a non object-type\n";
  throw std::out_of_range("");
}

JSON &JSON::Value(const std::string &key, JSON &defaultValue) {
  if (type == object) {
    auto &obj = GetObject();
    return obj.at(key) == JSON{} ? defaultValue : obj.at(key);
  }
  return defaultValue;
}

const JSON &JSON::Value(const std::string &key, JSON &defaultValue) const {
  if (type == object) {
    const auto &obj = GetObject();
    return obj.at(key) == JSON{} ? defaultValue : obj.at(key);
  }
  return defaultValue;
}

bool JSON::Contains(const JSON &value) const {
  if (type == object) {
    const auto &obj = GetObject();
    return std::ranges::find_if(
             obj, [&](const auto &pair) {
               return pair.second == value;
             }
           ) != obj.end();
  }

  if (type == array) {
    const auto &array = GetArray();
    return std::ranges::find(array, value) != array.end();
  }

  std::cout << "ERROR: JSON::Contains() called on non-array or non-object type\n";
  throw std::out_of_range("");
}

bool JSON::Contains(const std::string &key) const {
  if (type == object)
    return GetObject().contains(key);
  std::cout << "ERROR: JSON::Size() called on non-object type\n";
  throw std::out_of_range("");
}

bool JSON::Contains(const char *key) const {
  return Contains(std::string(key));
}

size_t JSON::Size() const {
  if (type == array)
    return GetArray().size();
  if (type == object)
    return GetObject().size();
  std::cout << "ERROR: JSON::Size() called on non-array or non-object type\n";
  throw std::out_of_range("");
}

bool JSON::Empty() const {
  if (type == array)
    return GetArray().empty();
  if (type == object)
    return GetObject().empty();
  std::cout << "ERROR: JSON::Empty() called on non-array or non-object type\n";
  throw std::out_of_range("");
}

void JSON::Clear() {
  if (type == array)
    std::get<JSONArray>(data).clear();
  else if (type == object)
    std::get<JSONObject>(data).clear();
}

bool JSON::IsNull() const {
  return type == Null;
}

bool JSON::IsBool() const {
  return type == Boolean;
}

bool JSON::IsNumber() const {
  return type == Number;
}

bool JSON::IsString() const {
  return type == String;
}

bool JSON::IsArray() const {
  return type == array;
}

bool JSON::IsObject() const {
  return type == object;
}

std::string JSON::Dump(const bool prettyPrint, const char indentChar) const {
  return dump(prettyPrint, 0, indentChar);
}

JSON JSON::Array(const std::initializer_list<JSON> &values) {
  JSON json;
  json.type = array;
  json.data = values;
  return json;
}

JSON JSON::Object(const std::initializer_list<std::pair<std::string, JSON>> &values) {
  JSON json;
  json.type = object;
  json.data = JSONObject(values.begin(), values.end());
  return json;
}

bool JSON::isComplexType() const {
  return type == object || type == array;
}

std::string JSON::dump(const bool prettyPrint, const int indentSize, const char indentChar) const {
  std::ostringstream oss;
  const int newIndent = prettyPrint ? indentSize + 1 : 0;
  const int endCharIndent = indentSize > 0 ? indentSize : 0;

  switch (type) {
    case Null:
      oss.write("null", 4);
      break;
    case Boolean: {
      const auto &value = GetBool();
      oss.write(value ? "true" : "false", value ? 4 : 5);
      break;
    }
    case Number: {
      if (const auto value = GetNumber(); std::isnan(value))
        oss.write("NaN", 3);
      else if (std::isinf(value))
        oss.write("Inf", 3);
      else
        oss << value << (static_cast<int>(value) == value ? ".0" : "");
      break;
    }
    case String: {
      const auto &value = GetString();
      oss.put('"');
      oss.write(value.c_str(), value.size());
      oss.put('"');
      break;
    }
    case array: {
      std::string dump;
      oss.put('[');
      for (const auto &item: std::get<JSONArray>(data)) {
        if (prettyPrint) {
          oss.put('\n');
          oss.write(std::string(newIndent * 2, indentChar).c_str(), newIndent * 2);
        }
        dump = item.dump(prettyPrint, newIndent, indentChar);
        oss.write(dump.c_str(), dump.size());
        oss.put(',');
      }
      if (!std::get<JSONArray>(data).empty()) {
        oss.seekp(-1, std::ios_base::end);
        if (prettyPrint) {
          oss.put('\n');
          oss.write(std::string(endCharIndent * 2, indentChar).c_str(), endCharIndent * 2);
        }
      }
      oss.put(']');
      break;
    }
    case object: {
      std::string dump;
      oss.put('{');
      for (const auto &[key, value]: std::get<JSONObject>(data)) {
        if (prettyPrint) {
          oss.put('\n');
          oss.write(std::string(newIndent * 2, indentChar).c_str(), newIndent * 2);
        }
        oss.put('"');
        oss.write(key.c_str(), key.size());
        oss.put('"');
        oss.put(':');
        if (prettyPrint)
          oss.put(' ');
        dump = value.dump(prettyPrint, newIndent, indentChar);
        oss.write(dump.c_str(), dump.size());
        oss.put(',');
      }
      if (!std::get<JSONObject>(data).empty()) {
        oss.seekp(-1, std::ios_base::end);
        if (prettyPrint) {
          oss.put('\n');
          oss.write(std::string(endCharIndent * 2, indentChar).c_str(), endCharIndent * 2);
        }
      }
      oss.put('}');
      break;
    }
  }

  return oss.str();
}
