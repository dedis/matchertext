// Strictly balanced comment: ([{}])
// Relaxed should differ here: ([)]

/*
 * Block comment with nested-looking content:
 * begin({[]})
 * broken([)]
 */

int ComputeValue() {
  // A string and a comment on the same line.
  const auto label = "comment-aware";
  return label[0];
}
