# Statistics Descriptions

## Embedded Statistics

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

## File Statistics

| Statistic                  | Description                                                            |
|----------------------------|------------------------------------------------------------------------|
| Sample Size                | Number of files processed.                                             |
| With Violation             | Number of files containing at least one MatcherText violation.         |
| Total Violations           | Total violation count accumulated across all files.                    |
| Maximum Violations         | Highest violation count observed in a single file.                     |
| Average Violations         | Average violation count per file.                                      |
| Compliance Rate            | Percentage of files without non-compliance.                            |
| With Violation Relaxed     | Number of files containing at least one relaxed MatcherText violation. |
| Total Relaxed Violations   | Total relaxed violation count accumulated across all files.            |
| Maximum Relaxed Violations | Highest relaxed violation count observed in a single file.             |
| Average Relaxed Violations | Average relaxed violation count per file.                              |
| Compliance Rate Relaxed    | Percentage of files without relaxed non-compliance.                    |

## Language Classification Statistics

| Metric     | Description                                                                 |
|------------|-----------------------------------------------------------------------------|
| Count      | Number of string literal samples assigned to the language bucket.           |
| %          | Share of samples assigned to the bucket within the corresponding table.     |
| Violations | Total MatcherText violation count accumulated by samples in the bucket.     |
| Toothpicks | Total toothpick count accumulated by samples in the bucket.                 |

## Language Bucket Definitions

| Bucket                                               | Description                                                                                          |
|------------------------------------------------------|------------------------------------------------------------------------------------------------------|
| Unknown                                              | Abstain bucket used when no detector or statistical model has enough evidence to make a safe call.   |
| PlainText                                            | Natural-language prose, log-like text, labels, and general human-readable strings.                   |
| FormatString                                         | `printf`-style or placeholder-driven message templates.                                              |
| FilePath                                             | Filesystem paths or path-like resource names.                                                        |
| URL                                                  | URLs and URI-like strings.                                                                           |
| HexData                                              | Hex-encoded payloads, hashes, or long grouped hexadecimal data.                                      |
| BinaryData                                           | Escaped byte blobs or strings dominated by non-printable/binary content.                             |
| JSON / YAML / XML / HTML / CSS / Shell / SQL / Regex | Structured-data or mini-language buckets matched by dedicated detectors and the fallback classifier. |
| Named programming languages                          | Statistical fallback labels for snippets that most closely resemble the corresponding language.      |

# Analysed Repositories

## Test Directory

### 1. Embedded Statistics

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

### 2. Nesting Histogram

| Level | Strings Raw | Strings Valid | Documentation Raw | Documentation Valid | Documentation Relaxed Raw | Documentation Relaxed Valid |
|-------|-------------|---------------|-------------------|---------------------|---------------------------|-----------------------------|
| 1     | 4           | 4             | 0                 | 0                   | 0                         | 0                           |
| 2     | 2           | 2             | 1                 | 1                   | 1                         | 1                           |
| 3     | 1           | 1             | 2                 | 2                   | 2                         | 2                           |

### 3. File Level Statistics

| Metric                     | File Stats  |
|----------------------------|-------------|
| Sample Size                | 3           |
| With Violation             | 1           |
| Total Violations           | 4           |
| Maximum Violations         | 4           |
| Average Violations         | 1.33333     |
| Compliance Rate            | 66.6667     |
| With Violation Relaxed     | 0           |
| Total Relaxed Violations   | 0           |
| Maximum Relaxed Violations | 0           |
| Average Relaxed Violations | 0           |
| Compliance Rate Relaxed    | 100         |

## Linux

### 1. Embedded Statistics

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

### 2. Nesting Histogram

| Level  | Strings Raw   | Strings Valid  | Documentation Raw   | Documentation Valid  | Documentation Relaxed Raw  | Documentation Relaxed Valid |
|--------|---------------|----------------|---------------------|----------------------|----------------------------|-----------------------------|
| 1      | 87660         | 85704          | 276002              | 274482               | 276204                     | 274944                      |
| 2      | 2068          | 1967           | 6465                | 6362                 | 6331                       | 6284                        |
| 3      | 217           | 205            | 573                 | 551                  | 541                        | 530                         |
| 4      | 49            | 46             | 147                 | 134                  | 130                        | 126                         |
| 5      | 1             | 1              | 36                  | 36                   | 31                         | 32                          |
| 6      | 2             | 1              | 18                  | 9                    | 16                         | 9                           |
| 7      | 1             | 1              | 10                  | 9                    | 6                          | 6                           |
| 8      | 2             | 2              | 4                   | 5                    | 0                          | 0                           |
| 9      | 0             | 0              | 2                   | 1                    | 0                          | 0                           |
| 10     | 0             | 0              | 1                   | 0                    | 0                          | 0                           |
| 11     | 1             | 0              | 3                   | 2                    | 2                          | 1                           |
| 12     | 1             | 0              | 0                   | 0                    | 0                          | 0                           |

### 3. File Level Statistics

| Metric                     | File Stats  |
|----------------------------|-------------|
| Sample Size                | 63071       |
| With Violation             | 3859        |
| Total Violations           | 15401       |
| Maximum Violations         | 518         |
| Average Violations         | 0.244185    |
| Compliance Rate            | 93.8815     |
| With Violation Relaxed     | 3652        |
| Total Relaxed Violations   | 13761       |
| Maximum Relaxed Violations | 518         |
| Average Relaxed Violations | 0.218183    |
| Compliance Rate Relaxed    | 94.2097     |

### 4. String Language Breakdown

| String Language   | Count   | %      | Violations   | Toothpicks  |
|-------------------|---------|--------|--------------|-------------|
| Unknown           | 589469  | 41.38% | 3230         | 83173       |
| FormatString      | 271068  | 19.03% | 1002         | 233581      |
| PlainText         | 162534  | 11.41% | 439          | 112203      |
| FilePath          | 7883    | 0.55%  | 11           | 79          |
| BinaryData        | 3976    | 0.28%  | 18           | 290090      |
| Shell             | 1997    | 0.14%  | 72           | 2436        |
| SQL               | 1095    | 0.08%  | 0            | 703         |
| HexData           | 433     | 0.03%  | 0            | 2           |
| YAML              | 417     | 0.03%  | 1            | 5342        |
| C++               | 279     | 0.02%  | 0            | 285         |
| HTML              | 98      | 0.01%  | 2            | 345         |
| CSS               | 88      | 0.01%  | 0            | 33          |
| Regex             | 86      | 0.01%  | 9            | 172         |
| XML               | 83      | 0.01%  | 2            | 80          |
| C                 | 80      | 0.01%  | 0            | 82          |
| URL               | 56      | 0.00%  | 0            | 38          |
| Objective-C       | 35      | 0.00%  | 0            | 10          |
| OCaml             | 24      | 0.00%  | 0            | 11          |
| C#                | 23      | 0.00%  | 0            | 6           |
| Go                | 14      | 0.00%  | 0            | 7           |
| JSON              | 8       | 0.00%  | 2            | 31          |
| PHP               | 7       | 0.00%  | 0            | 1           |
| Haskell           | 7       | 0.00%  | 0            | 11          |
| JavaScript        | 6       | 0.00%  | 2            | 3           |
| Swift             | 4       | 0.00%  | 0            | 2           |
| Python            | 4       | 0.00%  | 0            | 2           |
| R                 | 2       | 0.00%  | 0            | 0           |
| Java              | 2       | 0.00%  | 0            | 1           |
| HLSL              | 1       | 0.00%  | 0            | 0           |

Parsing takes around 1600-2300 ms

## Chromium

### 1. Embedded Statistics

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

### 2. Nesting Histogram

| Level  | Strings Raw   | Strings Valid  | Documentation Raw | Documentation Valid | Documentation Relaxed Raw | Documentation Relaxed Valid |
|--------|---------------|----------------|-------------------|---------------------|---------------------------|-----------------------------|
| 1      | 70395         | 60298          | 395059            | 355587              | 395253                    | 356657                      |
| 2      | 17947         | 16616          | 12468             | 11821               | 12354                     | 11832                       |
| 3      | 8548          | 8081           | 995               | 917                 | 953                       | 907                         |
| 4      | 3592          | 3426           | 181               | 159                 | 158                       | 157                         |
| 5      | 1316          | 1278           | 40                | 38                  | 39                        | 37                          |
| 6      | 700           | 684            | 15                | 5                   | 5                         | 5                           |
| 7      | 389           | 388            | 4                 | 4                   | 3                         | 3                           |
| 8      | 146           | 137            | 5                 | 3                   | 2                         | 2                           |
| 9      | 64            | 63             | 0                 | 0                   | 0                         | 0                           |
| 10     | 50            | 49             | 0                 | 0                   | 0                         | 0                           |
| 11     | 39            | 39             | 0                 | 0                   | 0                         | 0                           |
| 12     | 11            | 11             | 0                 | 0                   | 0                         | 0                           |
| 13     | 1             | 1              | 0                 | 0                   | 0                         | 0                           |
| 15     | 2             | 2              | 0                 | 0                   | 0                         | 0                           |
| 18     | 1             | 1              | 0                 | 0                   | 0                         | 0                           |
| 21     | 2             | 1              | 0                 | 0                   | 0                         | 0                           |
| 31     | 2             | 1              | 0                 | 0                   | 0                         | 0                           |
| 32     | 1             | 0              | 0                 | 0                   | 0                         | 0                           |
| 152    | 0             | 1              | 0                 | 0                   | 0                         | 0                           |
| 168    | 1             | 0              | 0                 | 0                   | 0                         | 0                           |
| 200    | 1             | 1              | 0                 | 0                   | 0                         | 0                           |
| 256    | 1             | 0              | 0                 | 0                   | 0                         | 0                           |

### 3. File Level Statistics

| Metric                     | File Stats   |
|----------------------------|--------------|
| Sample Size                | 129383       |
| With Violation             | 21925        |
| Total Violations           | 159956       |
| Maximum Violations         | 30795        |
| Average Violations         | 1.2363       |
| Compliance Rate            | 83.0542      |
| With Violation Relaxed     | 21729        |
| Total Relaxed Violations   | 156866       |
| Maximum Relaxed Violations | 30795        |
| Average Relaxed Violations | 1.21242      |
| Compliance Rate Relaxed    | 83.2057      |

### 4. String Language Breakdown

| String Language  | Count   |  %     | Violations | Toothpicks |
|------------------|---------|--------|------------|------------|
| Unknown          | 641015  | 26.10% | 52464      | 96413      |
| PlainText        | 128404  | 5.23%  | 4067       | 25128      |
| URL              | 74559   | 3.04%  | 39         | 798        |
| FilePath         | 36199   | 1.47%  | 47         | 2798       |
| FormatString     | 15932   | 0.65%  | 315        | 7683       |
| HTML             | 13774   | 0.56%  | 185        | 8801       |
| JSON             | 11026   | 0.45%  | 528        | 31587      |
| Shell            | 6158    | 0.25%  | 609        | 8912       |
| SQL              | 4684    | 0.19%  | 86         | 120        |
| HexData          | 4465    | 0.18%  | 0          | 7          |
| YAML             | 3800    | 0.15%  | 135        | 13186      |
| BinaryData       | 2734    | 0.11%  | 338        | 1002162    |
| Regex            | 2212    | 0.09%  | 1146       | 15309      |
| CSS              | 1461    | 0.06%  | 67         | 101        |
| XML              | 438     | 0.02%  | 159        | 703        |
| Objective-C      | 244     | 0.01%  | 4          | 7          |
| JavaScript       | 236     | 0.01%  | 226        | 38         |
| C++              | 52      | 0.00%  | 4          | 31         |
| OCaml            | 38      | 0.00%  | 3          | 5          |
| GLSL             | 37      | 0.00%  | 1          | 119        |
| C#               | 34      | 0.00%  | 8          | 0          |
| Swift            | 34      | 0.00%  | 0          | 6          |
| Python           | 26      | 0.00%  | 0          | 11         |
| Java             | 25      | 0.00%  | 5          | 6          |
| C                | 18      | 0.00%  | 0          | 7          |
| Go               | 17      | 0.00%  | 0          | 0          |
| R                | 14      | 0.00%  | 1          | 0          |
| Erlang           | 10      | 0.00%  | 0          | 0          |
| Elixir           | 5       | 0.00%  | 1          | 1          |
| Dart             | 5       | 0.00%  | 0          | 0          |
| Perl             | 4       | 0.00%  | 0          | 0          |
| HLSL             | 4       | 0.00%  | 0          | 0          |
| Scala            | 3       | 0.00%  | 0          | 0          |
| Kotlin           | 2       | 0.00%  | 0          | 4          |
| Ruby             | 2       | 0.00%  | 0          | 0          |
| Haskell          | 1       | 0.00%  | 0          | 1          |

Parsing takes around 2900-3200 ms
