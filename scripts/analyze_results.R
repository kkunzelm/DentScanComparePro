# ============================================================
# DentScanComparePro Results Analysis - readable console version v2
# ============================================================
# Purpose:
#   Results are printed directly after the commands that produce them.
#   No result tables are written as CSV files.
#
# Important changes in v2:
#   - Prevents the precision analysis from crashing.
#   - Trueness data have 5 observations per Scanner x SKD cell, so the
#     full model Outcome ~ Scanner * SKD is estimable.
#   - Precision data have only 1 aggregated observation per Scanner x SKD cell,
#     so the full interaction model is saturated. Precision outcomes are
#     therefore evaluated descriptively and with the additive exploratory model
#     Outcome ~ Scanner + SKD, not Scanner * SKD.
#   - Plots are produced before any optional long exploratory sections.
#   - Plot files are saved by default to output/plots.
# ============================================================

# ----------------------------
# 0. SETTINGS
# ----------------------------
# Optional: set this to your project directory if needed.
setwd("//KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOSScannerComparison/results_DentScanComparePro_masked/")

SAVE_PLOTS <- TRUE
PLOT_ALL_TRUENESS_METRICS <- TRUE
PLOT_PRECISION_METRICS <- TRUE
RUN_POSTHOC_FOR_OTHER_TRUENESS_METRICS <- FALSE  # keeps console output readable
PRINT_ALL_SCANNER_SKD_ROWS <- FALSE              # set TRUE to print all 36 Scanner x SKD rows

# ----------------------------
# 1. PACKAGES AND PATHS
# ----------------------------
required_packages <- c("tidyverse", "emmeans", "effectsize")
for (pkg in required_packages) {
  if (!requireNamespace(pkg, quietly = TRUE)) install.packages(pkg)
}

library(tidyverse)
library(emmeans)
library(effectsize)

options(width = 140)

data_dir <- "./data"
if (!file.exists(file.path(data_dir, "trueness_metrics.csv"))) {
  data_dir <- "."
}

plot_dir <- file.path("output", "plots")
if (SAVE_PLOTS && !dir.exists(plot_dir)) dir.create(plot_dir, recursive = TRUE)

section <- function(title) {
  cat("\n\n", paste(rep("=", 72), collapse = ""), "\n", sep = "")
  cat(title, "\n")
  cat(paste(rep("=", 72), collapse = ""), "\n\n", sep = "")
}

subsection <- function(title) {
  cat("\n", paste(rep("-", 60), collapse = ""), "\n", sep = "")
  cat(title, "\n")
  cat(paste(rep("-", 60), collapse = ""), "\n", sep = "")
}

print_table <- function(x, n = NULL) {
  if (is.null(n)) {
    print(x, width = Inf)
  } else {
    print(x, n = n, width = Inf)
  }
}

save_or_print_plot <- function(plot_object, filename = NULL, width = 10, height = 6) {
  print(plot_object)
  if (SAVE_PLOTS && !is.null(filename)) {
    ggsave(file.path(plot_dir, filename), plot = plot_object, width = width, height = height)
    cat("Saved plot:", file.path(plot_dir, filename), "\n")
  }
}

safe_eta_squared <- function(model) {
  out <- tryCatch(
    eta_squared(model, partial = TRUE),
    error = function(e) e
  )
  if (inherits(out, "error")) {
    cat("Effect sizes skipped:", conditionMessage(out), "\n")
  } else {
    print(out)
  }
}

safe_emmeans_pairs <- function(model, term) {
  out <- tryCatch(
    {
      emm <- emmeans(model, as.formula(paste("~", term)))
      pairs(emm, adjust = "tukey")
    },
    error = function(e) e
  )
  if (inherits(out, "error")) {
    cat("Post-hoc comparisons for", term, "skipped:", conditionMessage(out), "\n")
  } else {
    print(out)
  }
}

# ----------------------------
# 2. LOAD DATA
# ----------------------------
section("1. Load data")

trueness <- readr::read_csv(file.path(data_dir, "trueness_metrics.csv"), show_col_types = FALSE)
precision <- readr::read_csv(file.path(data_dir, "precision_matrix.csv"), show_col_types = FALSE)
summary_stats <- readr::read_csv(file.path(data_dir, "summary_by_scanner_skd.csv"), show_col_types = FALSE)

cat("Data directory used:", normalizePath(data_dir), "\n\n")
cat("Columns in trueness_metrics.csv:\n")
print(names(trueness))
cat("\nColumns in precision_metrics.csv:\n")
print(names(precision))
cat("\nColumns in summary_stats.csv:\n")
print(names(summary_stats))

# ----------------------------
# 3. CLEAN AND VALIDATE DATA
# ----------------------------
section("2. Clean and validate data")

subsection("Scanner-name check before trimming whitespace")
scanner_check <- trueness %>%
  mutate(
    Scanner_raw = Scanner_Model,
    Scanner_trimmed = trimws(Scanner_Model),
    nchar_raw = nchar(Scanner_raw),
    nchar_trimmed = nchar(Scanner_trimmed),
    has_whitespace = nchar_raw != nchar_trimmed
  ) %>%
  distinct(Scanner_raw, Scanner_trimmed, nchar_raw, nchar_trimmed, has_whitespace)
print_table(scanner_check)

if (any(scanner_check$has_whitespace)) {
  cat("\nWARNING: Some scanner names have leading/trailing whitespace. trimws() will be applied.\n")
}

# Ignore SKD 18.
skd_levels <- c(20, 22, 24, 26, 28, 30)

trueness <- trueness %>%
  mutate(Scanner_Model = trimws(Scanner_Model)) %>%
  filter(SKD_Value %in% skd_levels) %>%
  mutate(
    Scanner = factor(Scanner_Model),
    SKD = factor(SKD_Value, levels = skd_levels, ordered = TRUE)
  )

precision <- precision %>%
  mutate(Scanner_Model = trimws(Scanner_Model)) %>%
  filter(SKD_Value %in% skd_levels) %>%
  mutate(
    Scanner = factor(Scanner_Model),
    SKD = factor(SKD_Value, levels = skd_levels, ordered = TRUE)
  )

summary_stats <- summary_stats %>%
  mutate(Scanner_Model = trimws(Scanner_Model)) %>%
  filter(SKD_Value %in% skd_levels) %>%
  mutate(
    Scanner = factor(Scanner_Model),
    SKD = factor(SKD_Value, levels = skd_levels, ordered = TRUE)
  )

subsection("Sample sizes per Scanner x SKD, after ignoring SKD 18")
sample_sizes <- trueness %>%
  count(Scanner_Model, SKD_Value, name = "N") %>%
  pivot_wider(names_from = SKD_Value, values_from = N, values_fill = 0)
print_table(sample_sizes)

subsection("Cells with N different from 5")
missing_check <- trueness %>%
  count(Scanner_Model, SKD_Value) %>%
  filter(n != 5)

if (nrow(missing_check) > 0) {
  print_table(missing_check)
} else {
  cat("All Scanner x SKD cells have N = 5.\n")
}

subsection("Precision rows per Scanner x SKD")
precision_cell_counts <- precision %>%
  count(Scanner_Model, SKD_Value, name = "N") %>%
  pivot_wider(names_from = SKD_Value, values_from = N, values_fill = 0)
print_table(precision_cell_counts)
cat("\nInterpretation: precision_metrics.csv contains one aggregated precision row per Scanner x SKD cell.\n")
cat("Therefore a Scanner x SKD interaction model for precision has no residual degrees of freedom and is not meaningful.\n")

required_trueness_columns <- c("Scanner_Model", "SKD_Value", "RMS_mm", "Coverage_pct")
missing_required <- setdiff(required_trueness_columns, names(trueness))
if (length(missing_required) > 0) {
  stop("Missing required columns in trueness_metrics.csv: ", paste(missing_required, collapse = ", "))
}

# ----------------------------
# 4. DEFINE VARIABLES TO EVALUATE
# ----------------------------
section("3. Define variables to evaluate")

trueness_metrics <- c(
  "Triangles", "Edge_mm", "AspRatio", "ATI", "DensHighK", "DensLowK",
  "RMS_mm", "MAD_mm", "H100_mm", "H95_mm", "Bias_mm", "Coverage_pct",
  "Boundary_mm", "Holes", "Stitch_deg", "Vertices_Included", "Vertices_Total"
)
trueness_metrics <- intersect(trueness_metrics, names(trueness))

precision_metrics <- c(
  "Precision_MeanRMS_mm", "Precision_SD_mm", "Coefficient_of_Variation", "Pairwise_Count"
)
precision_metrics <- intersect(precision_metrics, names(precision))

cat("Trueness/mesh variables analyzed as outcomes:\n")
print(trueness_metrics)
cat("\nPrecision variables analyzed as outcomes:\n")
print(precision_metrics)
cat("\nNot analyzed as numeric outcomes: File_Path and text/identifier fields.\n")
cat("Note: Holes is a count variable; the ANOVA is exploratory and should be interpreted cautiously.\n")

# ----------------------------
# 5. PLOT FUNCTIONS
# ----------------------------
plot_box_by_scanner <- function(data, outcome, title_prefix) {
  ggplot(data, aes(x = reorder(Scanner, .data[[outcome]], FUN = median), y = .data[[outcome]])) +
    geom_boxplot(alpha = 0.7, outlier.alpha = 0.5) +
    geom_jitter(width = 0.2, alpha = 0.3) +
    labs(
      title = paste(title_prefix, outcome, "by Scanner"),
      x = "Scanner",
      y = outcome
    ) +
    coord_flip() +
    theme_minimal()
}

plot_interaction <- function(data, outcome, title_prefix) {
  ggplot(data, aes(x = SKD, y = .data[[outcome]], color = Scanner, group = Scanner)) +
    stat_summary(fun = mean, geom = "line", linewidth = 1) +
    stat_summary(fun = mean, geom = "point", size = 3) +
    stat_summary(fun.data = mean_se, geom = "errorbar", width = 0.2) +
    labs(
      title = paste(title_prefix, outcome, "by SKD and Scanner"),
      x = "SKD (mm)",
      y = outcome
    ) +
    theme_minimal() +
    theme(legend.position = "bottom")
}

plot_heatmap <- function(data, outcome, title_prefix, digits = 3) {
  data %>%
    group_by(Scanner, SKD) %>%
    summarise(Mean = mean(.data[[outcome]], na.rm = TRUE), .groups = "drop") %>%
    ggplot(aes(x = SKD, y = Scanner, fill = Mean)) +
    geom_tile() +
    geom_text(aes(label = sprintf(paste0("%.", digits, "f"), Mean)), size = 3) +
    scale_fill_viridis_c(option = "plasma", direction = -1) +
    labs(
      title = paste("Mean", title_prefix, outcome, "by Scanner and SKD"),
      fill = outcome
    ) +
    theme_minimal()
}

# ----------------------------
# 6. DESCRIPTIVE STATISTICS
# ----------------------------
section("4. Descriptive statistics")

subsection("Compact trueness/mesh summary by scanner: mean values")
trueness_means_by_scanner <- trueness %>%
  group_by(Scanner) %>%
  summarise(
    N = n(),
    across(all_of(trueness_metrics), ~ mean(.x, na.rm = TRUE), .names = "{.col}_mean"),
    .groups = "drop"
  )
print_table(trueness_means_by_scanner)

subsection("Compact precision summary by scanner: mean values")
precision_means_by_scanner <- precision %>%
  group_by(Scanner) %>%
  summarise(
    N = n(),
    across(all_of(precision_metrics), ~ mean(.x, na.rm = TRUE), .names = "{.col}_mean"),
    .groups = "drop"
  )
print_table(precision_means_by_scanner)

# ----------------------------
# 7. ANALYSIS FUNCTIONS
# ----------------------------
analyze_trueness_outcome <- function(data, outcome, do_posthoc = FALSE, print_cell_table = FALSE) {
  subsection(paste("Trueness/mesh outcome:", outcome))

  cat("Command: descriptive means by Scanner\n")
  by_scanner <- data %>%
    group_by(Scanner) %>%
    summarise(
      N = sum(!is.na(.data[[outcome]])),
      Mean = mean(.data[[outcome]], na.rm = TRUE),
      SD = sd(.data[[outcome]], na.rm = TRUE),
      Min = min(.data[[outcome]], na.rm = TRUE),
      Max = max(.data[[outcome]], na.rm = TRUE),
      .groups = "drop"
    ) %>%
    arrange(Mean)
  print_table(by_scanner)

  if (print_cell_table) {
    cat("\nCommand: descriptive means by Scanner x SKD\n")
    by_cell <- data %>%
      group_by(Scanner, SKD) %>%
      summarise(
        N = sum(!is.na(.data[[outcome]])),
        Mean = mean(.data[[outcome]], na.rm = TRUE),
        SD = sd(.data[[outcome]], na.rm = TRUE),
        Min = min(.data[[outcome]], na.rm = TRUE),
        Max = max(.data[[outcome]], na.rm = TRUE),
        .groups = "drop"
      )
    print_table(by_cell, n = if (PRINT_ALL_SCANNER_SKD_ROWS) Inf else 12)
  }

  n_unique <- data %>% pull(all_of(outcome)) %>% na.omit() %>% unique() %>% length()
  if (n_unique < 2) {
    cat("\nModel skipped: fewer than 2 unique non-missing values.\n")
    return(invisible(NULL))
  }

  formula_text <- paste(outcome, "~ Scanner * SKD")
  cat("\nCommand: model <- aov(", formula_text, ", data = trueness)\n", sep = "")
  model <- aov(as.formula(formula_text), data = data)

  cat("\nResult: ANOVA table\n")
  print(summary(model))

  cat("\nResult: partial eta-squared effect sizes\n")
  safe_eta_squared(model)

  if (do_posthoc) {
    cat("\nResult: Tukey-adjusted pairwise comparisons for Scanner\n")
    safe_emmeans_pairs(model, "Scanner")

    cat("\nResult: Tukey-adjusted pairwise comparisons for SKD\n")
    safe_emmeans_pairs(model, "SKD")
  } else {
    cat("\nPost-hoc comparisons skipped for this outcome to keep output readable.\n")
    cat("Set RUN_POSTHOC_FOR_OTHER_TRUENESS_METRICS <- TRUE if you want them.\n")
  }

  invisible(model)
}

analyze_precision_outcome <- function(data, outcome, do_posthoc = TRUE) {
  subsection(paste("Precision outcome:", outcome))

  cat("Command: descriptive values by Scanner x SKD\n")
  by_cell <- data %>%
    group_by(Scanner, SKD) %>%
    summarise(
      N = sum(!is.na(.data[[outcome]])),
      Value = mean(.data[[outcome]], na.rm = TRUE),
      .groups = "drop"
    )
  print_table(by_cell, n = if (PRINT_ALL_SCANNER_SKD_ROWS) Inf else 12)

  n_unique <- data %>% pull(all_of(outcome)) %>% na.omit() %>% unique() %>% length()
  if (n_unique < 2) {
    cat("\nModel skipped: fewer than 2 unique non-missing values.\n")
    return(invisible(NULL))
  }

  if (all(is.na(data[[outcome]])) || sd(data[[outcome]], na.rm = TRUE) == 0) {
    cat("\nModel skipped: outcome is constant.\n")
    return(invisible(NULL))
  }

  cat("\nImportant: precision data have one aggregated row per Scanner x SKD cell.\n")
  cat("A Scanner * SKD interaction model is saturated and therefore skipped.\n")

  formula_text <- paste(outcome, "~ Scanner + SKD")
  cat("\nCommand: model <- aov(", formula_text, ", data = precision)\n", sep = "")
  model <- aov(as.formula(formula_text), data = data)

  cat("\nResult: additive exploratory ANOVA table\n")
  print(summary(model))

  cat("\nResult: partial eta-squared effect sizes\n")
  safe_eta_squared(model)

  if (do_posthoc) {
    cat("\nResult: Tukey-adjusted pairwise comparisons for Scanner\n")
    safe_emmeans_pairs(model, "Scanner")

    cat("\nResult: Tukey-adjusted pairwise comparisons for SKD\n")
    safe_emmeans_pairs(model, "SKD")
  }

  invisible(model)
}

# ----------------------------
# 8. PRIMARY ENDPOINT: RMS_mm
# ----------------------------
section("5. Primary endpoint: RMS_mm")
primary_model <- analyze_trueness_outcome(
  trueness,
  "RMS_mm",
  do_posthoc = TRUE,
  print_cell_table = TRUE
)

subsection("Primary RMS plots")
save_or_print_plot(plot_box_by_scanner(trueness, "RMS_mm", "Trueness"), "trueness_RMS_mm_by_scanner.png")
save_or_print_plot(plot_interaction(trueness, "RMS_mm", "Trueness"), "trueness_RMS_mm_interaction.png")
save_or_print_plot(plot_heatmap(trueness, "RMS_mm", "Trueness"), "trueness_RMS_mm_heatmap.png")

# ----------------------------
# 9. OTHER TRUENESS/MESH VARIABLES
# ----------------------------
section("6. Comparable analyses for other trueness/mesh variables")
other_trueness_metrics <- setdiff(trueness_metrics, "RMS_mm")
trueness_models <- list()
for (v in other_trueness_metrics) {
  trueness_models[[v]] <- analyze_trueness_outcome(
    trueness,
    v,
    do_posthoc = RUN_POSTHOC_FOR_OTHER_TRUENESS_METRICS,
    print_cell_table = FALSE
  )
}

if (PLOT_ALL_TRUENESS_METRICS) {
  subsection("Plots for other trueness/mesh variables")
  for (v in other_trueness_metrics) {
    save_or_print_plot(plot_box_by_scanner(trueness, v, "Trueness/mesh"), paste0("trueness_", v, "_by_scanner.png"))
    save_or_print_plot(plot_interaction(trueness, v, "Trueness/mesh"), paste0("trueness_", v, "_interaction.png"))
  }
}

# ----------------------------
# 10. PRECISION VARIABLES
# ----------------------------
section("7. Precision variables")
cat("Precision variables are already aggregated, with one value per Scanner x SKD cell.\n")
cat("Therefore the inferential analysis below uses additive models only: Outcome ~ Scanner + SKD.\n")

precision_models <- list()
for (v in precision_metrics) {
  precision_models[[v]] <- analyze_precision_outcome(precision, v, do_posthoc = TRUE)
}

if (PLOT_PRECISION_METRICS) {
  subsection("Precision plots")
  for (v in precision_metrics) {
    if (sd(precision[[v]], na.rm = TRUE) > 0) {
      save_or_print_plot(plot_box_by_scanner(precision, v, "Precision"), paste0("precision_", v, "_by_scanner.png"))
      save_or_print_plot(plot_interaction(precision, v, "Precision"), paste0("precision_", v, "_by_SKD_and_scanner.png"))
    } else {
      cat("Plot skipped for", v, ": outcome is constant.\n")
    }
  }
}

# ----------------------------
# 11. CORRELATION ANALYSES
# ----------------------------
section("8. Correlation analyses")

subsection("Correlation of each trueness/mesh variable with RMS_mm")
cor_with_rms <- purrr::map_dfr(setdiff(trueness_metrics, "RMS_mm"), function(v) {
  complete_n <- sum(complete.cases(trueness[["RMS_mm"]], trueness[[v]]))
  if (complete_n < 3 || sd(trueness[[v]], na.rm = TRUE) == 0) {
    return(tibble(variable = v, correlation_with_RMS_mm = NA_real_, p_value = NA_real_, n = complete_n))
  }
  test <- cor.test(trueness[["RMS_mm"]], trueness[[v]], method = "pearson")
  tibble(
    variable = v,
    correlation_with_RMS_mm = unname(test$estimate),
    p_value = test$p.value,
    n = complete_n
  )
}) %>%
  arrange(desc(abs(correlation_with_RMS_mm)))
print_table(cor_with_rms)

subsection("Compact trueness correlation matrix")
cat("Printed only for RMS-related variables to keep the console readable.\n")
rms_related_metrics <- intersect(c("RMS_mm", "MAD_mm", "H95_mm", "H100_mm", "Bias_mm", "Coverage_pct"), trueness_metrics)
trueness_cor <- trueness %>%
  select(all_of(rms_related_metrics)) %>%
  cor(use = "pairwise.complete.obs", method = "pearson")
print(round(trueness_cor, 3))

subsection("Precision correlation matrix")
precision_cor_metrics <- precision_metrics[sapply(precision[precision_metrics], function(x) sd(x, na.rm = TRUE) > 0)]
if (length(precision_cor_metrics) >= 2) {
  precision_cor <- precision %>%
    select(all_of(precision_cor_metrics)) %>%
    cor(use = "pairwise.complete.obs", method = "pearson")
  print(round(precision_cor, 3))
} else {
  cat("Precision correlation matrix skipped: fewer than two non-constant variables.\n")
}

# ----------------------------
# 12. SUMMARY_STATS CONSISTENCY CHECK
# ----------------------------
section("9. summary_stats.csv consistency check")

cat("This checks whether summary_stats.csv matches summaries recomputed from trueness_metrics.csv.\n")

recomputed_summary <- trueness %>%
  group_by(Scanner_Model, SKD_Value) %>%
  summarise(
    N_recomputed = n(),
    Mean_RMS_mm_recomputed = mean(RMS_mm, na.rm = TRUE),
    SD_RMS_mm_recomputed = sd(RMS_mm, na.rm = TRUE),
    Min_RMS_mm_recomputed = min(RMS_mm, na.rm = TRUE),
    Max_RMS_mm_recomputed = max(RMS_mm, na.rm = TRUE),
    .groups = "drop"
  )

summary_consistency <- summary_stats %>%
  select(Scanner_Model, SKD_Value, N, Mean_RMS_mm, SD_RMS_mm, Min_RMS_mm, Max_RMS_mm) %>%
  left_join(recomputed_summary, by = c("Scanner_Model", "SKD_Value")) %>%
  mutate(
    N_diff = N - N_recomputed,
    Mean_RMS_mm_diff = Mean_RMS_mm - Mean_RMS_mm_recomputed,
    SD_RMS_mm_diff = SD_RMS_mm - SD_RMS_mm_recomputed,
    Min_RMS_mm_diff = Min_RMS_mm - Min_RMS_mm_recomputed,
    Max_RMS_mm_diff = Max_RMS_mm - Max_RMS_mm_recomputed
  )

summary_differences <- summary_consistency %>%
  filter(
    abs(N_diff) > 0 |
      abs(Mean_RMS_mm_diff) > 1e-10 |
      abs(SD_RMS_mm_diff) > 1e-10 |
      abs(Min_RMS_mm_diff) > 1e-10 |
      abs(Max_RMS_mm_diff) > 1e-10
  )

if (nrow(summary_differences) > 0) {
  cat("Rows with non-zero differences beyond rounding tolerance:\n")
  print_table(summary_differences)
} else {
  cat("No meaningful differences found. summary_stats.csv agrees with recomputed RMS summaries.\n")
}

# ----------------------------
# 13. SHORT INTERPRETATION AID
# ----------------------------
section("10. Short interpretation aid")
cat("Primary RMS result: interpret Scanner as the main effect of interest.\n")
cat("From the output you pasted earlier, Scanner was significant for RMS_mm, while SKD alone was not.\n")
cat("Many mesh-construction variables, such as Triangles, Edge_mm, vertex counts, density, and boundary length, show very strong scanner effects.\n")
cat("These variables describe scanner/data-generation properties and should not all be interpreted as independent accuracy endpoints.\n")
cat("Precision results are exploratory because precision_metrics.csv contains aggregated cell-level values only.\n")

section("Analysis complete")
cat("No result tables were written as CSV files.\n")
cat("Results were printed directly after the corresponding commands.\n")
if (SAVE_PLOTS) {
  cat("Plots were saved to:", normalizePath(plot_dir), "\n")
}
