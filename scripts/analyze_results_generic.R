# ============================================================
# DentScanComparePro – Patient Study Analysis
# ============================================================
# Study design:
#
#   N scanners × M patients × K repetitions (unbalanced)
#
# Model overview:
#   - Group factor is Patient_ID (random), not SKD (fixed)
#   - Primary model: lmer(outcome ~ Scanner + (1|Patient_ID))
#     from lme4/lmerTest — accounts for patient as random effect
#   - Variance components reported via VarCorr()
#   - Precision model: aov(outcome ~ Scanner + Patient_ID)
#     (additive, one aggregated value per Scanner × Patient cell)
#   - No SKD level filtering — all patients included
#   - MaxAspRatio added to tessellation metrics
#   - Filenames: precision_metrics.csv, summary_stats.csv
# ============================================================

# ----------------------------
# 0. SETTINGS
# ----------------------------
SAVE_PLOTS <- TRUE
PLOT_ALL_TRUENESS_METRICS <- TRUE
PLOT_PRECISION_METRICS <- TRUE
RUN_POSTHOC_FOR_OTHER_TRUENESS_METRICS <- FALSE
PRINT_ALL_PATIENT_ROWS <- FALSE

# ----------------------------
# 1. PACKAGES AND PATHS
# ----------------------------
required_packages <- c("tidyverse", "emmeans", "effectsize", "lme4", "lmerTest")
for (pkg in required_packages) {
  if (!requireNamespace(pkg, quietly = TRUE)) install.packages(pkg)
}

library(tidyverse)
library(emmeans)
library(effectsize)
library(lme4)
library(lmerTest)   # gives p-values for lmer via Satterthwaite/KR

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
  if (is.null(n)) print(x, width = Inf) else print(x, n = n, width = Inf)
}

save_or_print_plot <- function(plot_object, filename = NULL, width = 10, height = 6) {
  print(plot_object)
  if (SAVE_PLOTS && !is.null(filename)) {
    ggsave(file.path(plot_dir, filename), plot = plot_object, width = width, height = height)
    cat("Saved plot:", file.path(plot_dir, filename), "\n")
  }
}

# ----------------------------
# 2. LOAD DATA
# ----------------------------
section("1. Load data")

trueness    <- readr::read_csv(file.path(data_dir, "trueness_metrics.csv"),  show_col_types = FALSE)
precision   <- readr::read_csv(file.path(data_dir, "precision_metrics.csv"), show_col_types = FALSE)
summary_stats <- readr::read_csv(file.path(data_dir, "summary_stats.csv"),   show_col_types = FALSE)

cat("Data directory used:", normalizePath(data_dir), "\n\n")
cat("Columns in trueness_metrics.csv:\n");  print(names(trueness))
cat("\nColumns in precision_metrics.csv:\n"); print(names(precision))
cat("\nColumns in summary_stats.csv:\n");    print(names(summary_stats))

# ----------------------------
# 3. CLEAN AND VALIDATE
# ----------------------------
section("2. Clean and validate data")

subsection("Scanner-name whitespace check")
scanner_check <- trueness %>%
  mutate(
    Scanner_raw      = Scanner_Model,
    Scanner_trimmed  = trimws(Scanner_Model),
    has_whitespace   = nchar(Scanner_raw) != nchar(Scanner_trimmed)
  ) %>%
  distinct(Scanner_raw, Scanner_trimmed, has_whitespace)
print_table(scanner_check)
if (any(scanner_check$has_whitespace))
  cat("\nWARNING: whitespace in scanner names — trimws() applied.\n")

trueness <- trueness %>%
  mutate(
    Scanner    = factor(trimws(Scanner_Model)),
    Patient_ID = factor(Group_ID)           # patient is the random grouping unit
  )

precision <- precision %>%
  mutate(
    Scanner    = factor(trimws(Scanner_Model)),
    Patient_ID = factor(Group_ID)
  )

summary_stats <- summary_stats %>%
  mutate(
    Scanner    = factor(trimws(Scanner_Model)),
    Patient_ID = factor(Group_ID)
  )

subsection("Sample sizes per Scanner × Patient_ID")
sample_sizes <- trueness %>%
  count(Scanner_Model, Patient_ID, name = "N") %>%
  pivot_wider(names_from = Patient_ID, values_from = N, values_fill = 0)
print_table(sample_sizes)

subsection("Cells with N different from expected repetitions")
missing_check <- trueness %>%
  count(Scanner_Model, Patient_ID) %>%
  filter(n != median(n))
if (nrow(missing_check) > 0) {
  print_table(missing_check)
} else {
  cat("All Scanner × Patient cells have the same N.\n")
}

subsection("Precision rows per Scanner × Patient_ID")
precision_cell_counts <- precision %>%
  count(Scanner_Model, Patient_ID, name = "N") %>%
  pivot_wider(names_from = Patient_ID, values_from = N, values_fill = 0)
print_table(precision_cell_counts)
cat("\nNote: precision_metrics.csv contains one aggregated precision row per Scanner × Patient cell.\n")
cat("A full interaction model for precision is saturated; additive aov() is used instead.\n")

required_cols <- c("Scanner_Model", "Group_ID", "RMS_mm", "Coverage_pct")
missing_required <- setdiff(required_cols, names(trueness))
if (length(missing_required) > 0)
  stop("Missing required columns: ", paste(missing_required, collapse = ", "))

# ----------------------------
# 4. DEFINE VARIABLES
# ----------------------------
section("3. Variables to evaluate")

trueness_metrics <- c(
  "Triangles", "Edge_mm", "AspRatio", "MaxAspRatio", "ATI", "DensHighK", "DensLowK",
  "RMS_mm", "MAD_mm", "H100_mm", "H95_mm", "Bias_mm", "Coverage_pct",
  "Boundary_mm", "Holes", "Stitch_deg", "Vertices_Included", "Vertices_Total"
)
trueness_metrics <- intersect(trueness_metrics, names(trueness))

precision_metrics_vars <- c(
  "Precision_MeanRMS_mm", "Precision_SD_mm", "Coefficient_of_Variation", "Pairwise_Count"
)
precision_metrics_vars <- intersect(precision_metrics_vars, names(precision))

cat("Trueness/mesh variables:\n"); print(trueness_metrics)
cat("\nPrecision variables:\n");    print(precision_metrics_vars)

# ----------------------------
# 5. PLOT FUNCTIONS
# ----------------------------
plot_box_by_scanner <- function(data, outcome, title_prefix) {
  ggplot(data, aes(x = reorder(Scanner, .data[[outcome]], FUN = median),
                   y = .data[[outcome]])) +
    geom_boxplot(alpha = 0.7, outlier.alpha = 0.5) +
    geom_jitter(width = 0.2, alpha = 0.3) +
    labs(title = paste(title_prefix, outcome, "by Scanner"),
         x = "Scanner", y = outcome) +
    coord_flip() +
    theme_minimal()
}

plot_patient_profiles <- function(data, outcome, title_prefix) {
  # Individual patient trajectories across scanners
  data %>%
    group_by(Scanner, Patient_ID) %>%
    summarise(Mean = mean(.data[[outcome]], na.rm = TRUE), .groups = "drop") %>%
    ggplot(aes(x = Scanner, y = Mean, group = Patient_ID, color = Patient_ID)) +
    geom_line(alpha = 0.5) +
    geom_point(size = 2) +
    stat_summary(aes(group = 1), fun = mean, geom = "line",
                 linewidth = 1.5, color = "black", linetype = "dashed") +
    labs(title = paste(title_prefix, outcome, "– patient profiles by Scanner"),
         subtitle = "Dashed line = grand mean; coloured lines = individual patients",
         x = "Scanner", y = outcome, color = "Patient") +
    theme_minimal() +
    theme(legend.position = "right")
}

plot_heatmap <- function(data, outcome, title_prefix, digits = 3) {
  data %>%
    group_by(Scanner, Patient_ID) %>%
    summarise(Mean = mean(.data[[outcome]], na.rm = TRUE), .groups = "drop") %>%
    ggplot(aes(x = Patient_ID, y = Scanner, fill = Mean)) +
    geom_tile() +
    geom_text(aes(label = sprintf(paste0("%.", digits, "f"), Mean)), size = 2.5) +
    scale_fill_viridis_c(option = "plasma", direction = -1) +
    labs(title = paste("Mean", title_prefix, outcome, "by Scanner and Patient"),
         fill = outcome) +
    theme_minimal() +
    theme(axis.text.x = element_text(angle = 45, hjust = 1))
}

# ----------------------------
# 6. DESCRIPTIVE STATISTICS
# ----------------------------
section("4. Descriptive statistics")

subsection("Trueness/mesh summary by Scanner (mean across all patients × repetitions)")
trueness_means_by_scanner <- trueness %>%
  group_by(Scanner) %>%
  summarise(
    N = n(),
    across(all_of(trueness_metrics), ~ mean(.x, na.rm = TRUE), .names = "{.col}_mean"),
    .groups = "drop"
  )
print_table(trueness_means_by_scanner)

subsection("Precision summary by Scanner")
precision_means_by_scanner <- precision %>%
  group_by(Scanner) %>%
  summarise(
    N = n(),
    across(all_of(precision_metrics_vars), ~ mean(.x, na.rm = TRUE), .names = "{.col}_mean"),
    .groups = "drop"
  )
print_table(precision_means_by_scanner)

# ----------------------------
# 7. ANALYSIS FUNCTIONS
# ----------------------------

# Variance component helper
print_variance_components <- function(model) {
  vc <- VarCorr(model)
  vc_df <- as.data.frame(vc)
  total_var <- sum(vc_df$vcov)
  vc_df <- vc_df %>%
    mutate(pct_total = 100 * vcov / total_var)
  cat("\nVariance components:\n")
  print(vc_df[, c("grp", "var1", "vcov", "sdcor", "pct_total")], digits = 4)
  cat(sprintf("  ICC (patient) = %.3f\n",
              vc_df$vcov[vc_df$grp != "Residual"][1] / total_var))
}

safe_emmeans_pairs <- function(model, term) {
  out <- tryCatch({
    emm <- emmeans(model, as.formula(paste("~", term)))
    pairs(emm, adjust = "tukey")
  }, error = function(e) e)
  if (inherits(out, "error")) {
    cat("Post-hoc for", term, "skipped:", conditionMessage(out), "\n")
  } else {
    print(out)
  }
}

analyze_trueness_outcome <- function(data, outcome, do_posthoc = FALSE,
                                     print_patient_table = FALSE) {
  subsection(paste("Trueness outcome:", outcome))

  cat("Descriptive means by Scanner:\n")
  by_scanner <- data %>%
    group_by(Scanner) %>%
    summarise(
      N    = sum(!is.na(.data[[outcome]])),
      Mean = mean(.data[[outcome]], na.rm = TRUE),
      SD   = sd(.data[[outcome]], na.rm = TRUE),
      Min  = min(.data[[outcome]], na.rm = TRUE),
      Max  = max(.data[[outcome]], na.rm = TRUE),
      .groups = "drop"
    ) %>%
    arrange(Mean)
  print_table(by_scanner)

  if (print_patient_table) {
    cat("\nDescriptive means by Scanner × Patient_ID:\n")
    by_cell <- data %>%
      group_by(Scanner, Patient_ID) %>%
      summarise(
        N    = sum(!is.na(.data[[outcome]])),
        Mean = mean(.data[[outcome]], na.rm = TRUE),
        SD   = sd(.data[[outcome]], na.rm = TRUE),
        .groups = "drop"
      )
    print_table(by_cell, n = if (PRINT_ALL_PATIENT_ROWS) Inf else 16)
  }

  n_unique <- data %>% pull(all_of(outcome)) %>% na.omit() %>% unique() %>% length()
  if (n_unique < 2) {
    cat("\nModel skipped: fewer than 2 unique non-missing values.\n")
    return(invisible(NULL))
  }

  # Mixed model: Scanner fixed, Patient random
  formula_text <- paste(outcome, "~ Scanner + (1|Patient_ID)")
  cat("\nModel: lmer(", formula_text, ", data = trueness)\n", sep = "")
  model <- tryCatch(
    lmer(as.formula(formula_text), data = data, REML = TRUE),
    error = function(e) { cat("lmer failed:", conditionMessage(e), "\n"); NULL }
  )
  if (is.null(model)) return(invisible(NULL))

  cat("\nFixed effects – ANOVA table (Satterthwaite df):\n")
  print(anova(model))

  print_variance_components(model)

  if (do_posthoc) {
    cat("\nTukey-adjusted pairwise comparisons for Scanner:\n")
    safe_emmeans_pairs(model, "Scanner")
  } else {
    cat("\nPost-hoc skipped. Set RUN_POSTHOC_FOR_OTHER_TRUENESS_METRICS <- TRUE to enable.\n")
  }

  invisible(model)
}

analyze_precision_outcome <- function(data, outcome, do_posthoc = TRUE) {
  subsection(paste("Precision outcome:", outcome))

  cat("Descriptive values by Scanner × Patient_ID:\n")
  by_cell <- data %>%
    group_by(Scanner, Patient_ID) %>%
    summarise(
      N     = sum(!is.na(.data[[outcome]])),
      Value = mean(.data[[outcome]], na.rm = TRUE),
      .groups = "drop"
    )
  print_table(by_cell, n = if (PRINT_ALL_PATIENT_ROWS) Inf else 16)

  n_unique <- data %>% pull(all_of(outcome)) %>% na.omit() %>% unique() %>% length()
  if (n_unique < 2 || sd(data[[outcome]], na.rm = TRUE) == 0) {
    cat("\nModel skipped.\n"); return(invisible(NULL))
  }

  cat("\nPrecision data: one aggregated row per Scanner × Patient cell.\n")
  cat("Additive exploratory ANOVA: ", outcome, "~ Scanner + Patient_ID\n", sep = "")
  model <- aov(as.formula(paste(outcome, "~ Scanner + Patient_ID")), data = data)
  cat("\nANOVA table:\n");  print(summary(model))

  out <- tryCatch(eta_squared(model, partial = TRUE), error = function(e) e)
  if (!inherits(out, "error")) { cat("\nPartial eta-squared:\n"); print(out) }

  if (do_posthoc) {
    cat("\nTukey pairwise – Scanner:\n")
    safe_emmeans_pairs(model, "Scanner")
  }

  invisible(model)
}

# ----------------------------
# 8. PRIMARY ENDPOINT: RMS_mm
# ----------------------------
section("5. Primary endpoint: RMS_mm")

primary_model <- analyze_trueness_outcome(
  trueness, "RMS_mm",
  do_posthoc = TRUE,
  print_patient_table = TRUE
)

subsection("Primary RMS plots")
save_or_print_plot(plot_box_by_scanner(trueness, "RMS_mm", "Trueness"),
                   "trueness_RMS_mm_by_scanner.png")
save_or_print_plot(plot_patient_profiles(trueness, "RMS_mm", "Trueness"),
                   "trueness_RMS_mm_patient_profiles.png", width = 10, height = 7)
save_or_print_plot(plot_heatmap(trueness, "RMS_mm", "Trueness"),
                   "trueness_RMS_mm_heatmap.png", width = 14, height = 5)

# ----------------------------
# 9. OTHER TRUENESS/MESH VARIABLES
# ----------------------------
section("6. Other trueness/mesh variables")

other_trueness_metrics <- setdiff(trueness_metrics, "RMS_mm")
trueness_models <- list()
for (v in other_trueness_metrics) {
  trueness_models[[v]] <- analyze_trueness_outcome(
    trueness, v,
    do_posthoc = RUN_POSTHOC_FOR_OTHER_TRUENESS_METRICS,
    print_patient_table = FALSE
  )
}

if (PLOT_ALL_TRUENESS_METRICS) {
  subsection("Plots for other trueness/mesh variables")
  for (v in other_trueness_metrics) {
    save_or_print_plot(
      plot_box_by_scanner(trueness, v, "Trueness/mesh"),
      paste0("trueness_", v, "_by_scanner.png")
    )
    save_or_print_plot(
      plot_patient_profiles(trueness, v, "Trueness/mesh"),
      paste0("trueness_", v, "_patient_profiles.png"), width = 10, height = 7
    )
  }
}

# ----------------------------
# 10. PRECISION VARIABLES
# ----------------------------
section("7. Precision variables")

precision_models <- list()
for (v in precision_metrics_vars) {
  precision_models[[v]] <- analyze_precision_outcome(precision, v, do_posthoc = TRUE)
}

if (PLOT_PRECISION_METRICS) {
  subsection("Precision plots")
  for (v in precision_metrics_vars) {
    if (sd(precision[[v]], na.rm = TRUE) > 0) {
      save_or_print_plot(
        plot_box_by_scanner(precision, v, "Precision"),
        paste0("precision_", v, "_by_scanner.png")
      )
    } else {
      cat("Plot skipped for", v, ": constant value.\n")
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
  if (complete_n < 3 || sd(trueness[[v]], na.rm = TRUE) == 0)
    return(tibble(variable = v, r = NA_real_, p = NA_real_, n = complete_n))
  test <- cor.test(trueness[["RMS_mm"]], trueness[[v]], method = "pearson")
  tibble(variable = v, r = unname(test$estimate), p = test$p.value, n = complete_n)
}) %>%
  arrange(desc(abs(r)))
print_table(cor_with_rms)

subsection("Correlation matrix – RMS-related accuracy metrics")
rms_related <- intersect(c("RMS_mm","MAD_mm","H95_mm","H100_mm","Bias_mm","Coverage_pct"),
                         trueness_metrics)
print(round(cor(trueness[rms_related], use = "pairwise.complete.obs"), 3))

subsection("Correlation matrix – tessellation metrics")
tess_metrics <- intersect(c("Triangles","Edge_mm","AspRatio","MaxAspRatio",
                              "ATI","DensHighK","DensLowK"), trueness_metrics)
if (length(tess_metrics) >= 2)
  print(round(cor(trueness[tess_metrics], use = "pairwise.complete.obs"), 3))

# ----------------------------
# 12. SUMMARY_STATS CONSISTENCY CHECK
# ----------------------------
section("9. summary_stats.csv consistency check")

recomputed <- trueness %>%
  group_by(Scanner_Model, Group_ID) %>%
  summarise(
    N_rc        = n(),
    Mean_RMS_rc = mean(RMS_mm, na.rm = TRUE),
    SD_RMS_rc   = sd(RMS_mm, na.rm = TRUE),
    .groups = "drop"
  )

diffs <- summary_stats %>%
  left_join(recomputed, by = c("Scanner_Model", "Group_ID")) %>%
  mutate(N_diff    = N - N_rc,
         Mean_diff = Mean_RMS_mm - Mean_RMS_rc,
         SD_diff   = SD_RMS_mm   - SD_RMS_rc) %>%
  filter(abs(N_diff) > 0 | abs(Mean_diff) > 1e-10 | abs(SD_diff) > 1e-10)

if (nrow(diffs) > 0) {
  cat("Rows with discrepancies:\n"); print_table(diffs)
} else {
  cat("No meaningful differences — summary_stats.csv agrees with recomputed values.\n")
}

# ----------------------------
# 13. INTERPRETATION AID
# ----------------------------
section("10. Interpretation guide")
cat(
"Study design: N scanners × M patients × K repetitions (unbalanced designs accepted).\n\n",
"Primary model: lmer(RMS_mm ~ Scanner + (1|Patient_ID))\n",
"  - Scanner: fixed effect (specific devices under comparison)\n",
"  - Patient_ID: random effect (biological sample, not exhaustive)\n",
"  - Variance components show how much of total variance is between patients vs. residual noise.\n",
"  - ICC (patient) >> 0 → patient anatomy dominates; Scanner effect must be interpreted\n",
"    relative to within-patient repeatability.\n\n",
"Precision model: aov(outcome ~ Scanner + Patient_ID) — additive, one value per cell.\n",
"  This is exploratory; the additive assumption may not hold.\n\n",
"Tessellation metrics (Triangles, Edge_mm, AspRatio, MaxAspRatio, ATI, DensHighK, DensLowK)\n",
"  describe the scanner's meshing strategy, not clinical accuracy.\n",
"  Correlate them with RMS_mm to test whether mesh density predicts trueness.\n"
)

section("Analysis complete")
if (SAVE_PLOTS) cat("Plots saved to:", normalizePath(plot_dir), "\n")
