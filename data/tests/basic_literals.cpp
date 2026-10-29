// Basic fixture for string literal extraction.

#include <string>

const auto kPlain = "hello (world)";
const auto kEscaped = "path\\\\to\\\\file";
const auto kWide = L"[wide]";
const auto kConcatenated = "left(" "right)";

std::string BuildMessage() {
  return std::string("alpha{beta}") + " and more text";
}
