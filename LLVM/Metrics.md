## Statistics Description

| Statistic                          | Description                                                                                             |
|------------------------------------|---------------------------------------------------------------------------------------------------------|
| Sample Size                        | Number of samples processed.                                                                            |
| With Toothpicks                    | Number of samples containing at least one toothpick.                                                    |
| Total Toothpicks                   | Total toothpick count accumulated across all samples.                                                   |
| Maximum Toothpicks                 | Highest toothpick count observed in a single sample.                                                    |
| Average Toothpicks                 | Average toothpick count per sample.                                                                     |
| Average With Toothpicks            | Average toothpick count among samples that contain toothpicks.                                          |
| With Non-Compliance                | Number of samples with at least one non-compliance.                                                     |
| Non-Compliance Count               | Total number of non-compliance events across all samples.                                               |
| Non-Compliance Max                 | Highest non-compliance count observed in a single sample.                                               |
| Avg Unmatched Matchers Per Sample  | Average non-compliance count per sample.                                                                |
| Compliance Rate                    | Percentage of samples without non-compliance.                                                           |
| With Raw Nested Embedding          | Number of samples whose raw nesting depth exceeds 1, even if the nesting is never closed.               |
| Sum Of Per-Sample Raw Max Depth    | Sum of each sample's maximum raw nesting depth, counting unmatched openers such as '((('.               |
| Highest Per-Sample Raw Max Depth   | Greatest raw nesting depth observed in any single sample, even if the nesting is left open.             |
| Avg Per-Sample Raw Max Depth       | Average of the maximum raw nesting depth measured per sample.                                           |
| With Valid Nested Embedding        | Number of samples whose nesting depth exceeds 1 and is confirmed by matching closers, such as '((()))'. |
| Sum Of Per-Sample Valid Max Depth  | Sum of each sample's maximum valid nesting depth, counting only depths closed by matching closers.      |
| Highest Per-Sample Valid Max Depth | Greatest valid nesting depth observed in any single sample, confirmed by matching closers.              |
| Avg Per-Sample Valid Max Depth     | Average of the maximum valid nesting depth measured per sample.                                         |
| Raw Character Count                | Total number of raw input characters processed.                                                         |

## Test Directory

| Metric                             | Strings  | Documentation   | Documentation Relaxed   |
|------------------------------------|----------|-----------------|-------------------------|
| Sample Size                        | 10       | 6               | 6                       |
| With Toothpicks                    | 3        | 0               | 0                       |
| Total Toothpicks                   | 18       | 0               | 0                       |
| Maximum Toothpicks                 | 8        | 0               | 0                       |
| Average Toothpicks                 | 1.8      | 0               | 0                       |
| Average With Toothpicks            | 6        | 0               | 0                       |
| With Non-Compliance                | 0        | 2               | 0                       |
| Non-Compliance Count               | 0        | 4               | 0                       |
| Non-Compliance Max                 | 0        | 2               | 0                       |
| Avg Unmatched Matchers Per Sample  | 0        | 0.666667        | 0                       |
| Compliance Rate                    | 100      | 66.6667         | 100                     |
| With Raw Nested Embedding          | 3        | 3               | 3                       |
| Sum Of Per-Sample Raw Max Depth    | 11       | 8               | 8                       |
| Highest Per-Sample Raw Max Depth   | 3        | 3               | 3                       |
| Avg Per-Sample Raw Max Depth       | 1.1      | 1.33333         | 1.33333                 |
| With Valid Nested Embedding        | 3        | 3               | 3                       |
| Sum Of Per-Sample Valid Max Depth  | 11       | 8               | 8                       |
| Highest Per-Sample Valid Max Depth | 3        | 3               | 3                       |
| Avg Per-Sample Valid Max Depth     | 1.1      | 1.33333         | 1.33333                 |
| Raw Character Count                | 180      | 285             | 285                     |

## Linux

| Metric                             | Strings     | Documentation   | Documentation Relaxed   |
|------------------------------------|-------------|-----------------|-------------------------|
| Sample Size                        | 1.42451e+06 | 2.30542e+06     | 2.30542e+06             |
| With Toothpicks                    | 378718      | 4482            | 4482                    |
| Total Toothpicks                   | 731348      | 21328           | 21328                   |
| Maximum Toothpicks                 | 6208        | 249             | 249                     |
| Average Toothpicks                 | 0.513404    | 0.00925123      | 0.00925123              |
| Average With Toothpicks            | 1.93111     | 4.75859         | 4.75859                 |
| With Non-Compliance                | 4237        | 6056            | 5499                    |
| Non-Compliance Count               | 4790        | 10611           | 8971                    |
| Non-Compliance Max                 | 12          | 49              | 49                      |
| Avg Unmatched Matchers Per Sample  | 0.00336257  | 0.00460263      | 0.00389126              |
| Compliance Rate                    | 99.7026     | 99.7373         | 99.7615                 |
| With Raw Nested Embedding          | 2342        | 7259            | 7057                    |
| Sum Of Per-Sample Raw Max Depth    | 92706       | 291690          | 291324                  |
| Highest Per-Sample Raw Max Depth   | 12          | 11              | 11                      |
| Avg Per-Sample Raw Max Depth       | 0.0650794   | 0.126523        | 0.126365                |
| With Valid Nested Embedding        | 2223        | 7109            | 6988                    |
| Sum Of Per-Sample Valid Max Depth  | 90471       | 289763          | 289873                  |
| Highest Per-Sample Valid Max Depth | 8           | 11              | 11                      |
| Avg Per-Sample Valid Max Depth     | 0.0635104   | 0.125688        | 0.125735                |
| Raw Character Count                | 2.76905e+07 | 1.74103e+08     | 1.74103e+08             |

Parsing takes around 1600-2300 ms

## Chromium

| Metric                             | Strings     | Documentation   | Documentation Relaxed   |
|------------------------------------|-------------|-----------------|-------------------------|
| Sample Size                        | 2.45626e+06 | 4.14514e+06     | 4.14514e+06             |
| With Toothpicks                    | 65708       | 11476           | 11476                   |
| Total Toothpicks                   | 1.21438e+06 | 18175           | 18175                   |
| Maximum Toothpicks                 | 244411      | 42              | 42                      |
| Average Toothpicks                 | 0.494402    | 0.00438466      | 0.00438466              |
| Average With Toothpicks            | 18.4814     | 1.58374         | 1.58374                 |
| With Non-Compliance                | 36078       | 94544           | 93368                   |
| Non-Compliance Count               | 60438       | 99518           | 96428                   |
| Non-Compliance Max                 | 256         | 23              | 23                      |
| Avg Unmatched Matchers Per Sample  | 0.0246057   | 0.0240084       | 0.0232629               |
| Compliance Rate                    | 98.5312     | 97.7192         | 97.7475                 |
| With Raw Nested Embedding          | 32814       | 13708           | 13514                   |
| Sum Of Per-Sample Raw Max Depth    | 163430      | 424062          | 423714                  |
| Highest Per-Sample Raw Max Depth   | 256         | 8               | 8                       |
| Avg Per-Sample Raw Max Depth       | 0.0665362   | 0.102304        | 0.10222                 |
| With Valid Nested Embedding        | 30780       | 12947           | 12943                   |
| Sum Of Per-Sample Valid Max Depth  | 147866      | 382888          | 383922                  |
| Highest Per-Sample Valid Max Depth | 200         | 8               | 8                       |
| Avg Per-Sample Valid Max Depth     | 0.0601997   | 0.0923705       | 0.0926199               |
| Raw Character Count                | 8.25084e+07 | 2.06343e+08     | 2.06343e+08             |

Parsing takes around 2900-3200 ms