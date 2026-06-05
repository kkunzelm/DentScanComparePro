  # ============================================================
# DentScanComparePro Results Analysis 
# ============================================================
 
# Load required packages
if (!require("tidyverse")) install.packages("tidyverse")
if (!require("emmeans")) install.packages("emmeans") 
if (!require("effectsize")) install.packages("effectsize") 
 
library(tidyverse)
library(emmeans) 
library(effectsize) 

setwd("/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results")  
   
# Set your output directory
output_dir <- "./output"  # Adjust to your actual path 
data_dir <- "./data"
 
# ============================================================
# 1. LOAD DATA
# ============================================================
 
trueness <- read_csv(file.path(data_dir, "trueness_metrics.csv"))
precision <- read_csv(file.path(data_dir, "precision_metrics.csv"))
summary_stats <- read_csv(file.path(data_dir, "summary_stats.csv"))

# ============================================================
# 1a. DATA SANITIZATION & VALIDATION
# ============================================================

cat("\n=== Data Validation ===\n\n")

# Check for hidden characters or whitespace in scanner names
cat("Unique scanner names (with character counts):\n")
scanner_check <- trueness %>%
  mutate(
    Scanner_raw = Scanner_Model,
    Scanner_trimmed = trimws(Scanner_Model),
    nchar_raw = nchar(Scanner_raw),
    nchar_trimmed = nchar(Scanner_trimmed),
    has_whitespace = nchar_raw != nchar_trimmed
  ) %>%
  distinct(Scanner_raw, Scanner_trimmed, nchar_raw, nchar_trimmed, has_whitespace)
print(scanner_check)

# Warn if whitespace issues found
if (any(scanner_check$has_whitespace)) {
  cat("\n⚠️  WARNING: Some scanner names have leading/trailing whitespace!\n")
  cat("   Applying trimws() to clean up...\n\n")
}

# Clean scanner names (trim whitespace)
trueness <- trueness %>%
  mutate(Scanner_Model = trimws(Scanner_Model))

precision <- precision %>%
  mutate(Scanner_Model = trimws(Scanner_Model))

# Check sample sizes per Scanner × SKD
cat("\nSample sizes per Scanner × SKD:\n")
sample_sizes <- trueness %>%
  count(Scanner_Model, SKD_Value, name = "N") %>%
  pivot_wider(names_from = SKD_Value, values_from = N, values_fill = 0)
print(sample_sizes)

# Check for missing data (expected: 5 per cell, except iTero has SKD 18)
cat("\nExpected: 5 observations per cell (iTeroLumina also has SKD 18)\n")
missing_check <- trueness %>%
  count(Scanner_Model, SKD_Value) %>%
  filter(n != 5)

if (nrow(missing_check) > 0) {
  cat("\n⚠️  WARNING: The following cells have N ≠ 5:\n")
  print(missing_check)
} else {
  cat("✓ All cells have N = 5\n")
}

# Total observations per scanner
cat("\nTotal observations per scanner:\n")
trueness %>%
  count(Scanner_Model, name = "Total_N") %>%
  print()

cat("\n=== End Data Validation ===\n\n")

# Convert to factors (after cleaning)
trueness <- trueness %>%
  mutate(
    Scanner = factor(Scanner_Model),
    SKD = factor(SKD_Value, levels = c(18, 20, 22, 24, 26, 28, 30), ordered = TRUE)
  )
 
# ============================================================
# 2. DESCRIPTIVE STATISTICS
# ============================================================
 
# Overall summary by scanner 
trueness %>%
 group_by(Scanner) %>%
 summarise(
N = n(),
Mean_RMS = mean(Trueness_RMS_mm), 
SD_RMS = sd(Trueness_RMS_mm),
Min_RMS = min(Trueness_RMS_mm),
Max_RMS = max(Trueness_RMS_mm),
Mean_Coverage = mean(Coverage_Rate_pct) 
 ) %>% 
 arrange(Mean_RMS) %>%
 print()
 
# Summary by Scanner × SKD
trueness %>%
 group_by(Scanner, SKD) %>% 
 summarise(
N = n(),
Mean_RMS = mean(Trueness_RMS_mm), 
SD_RMS = sd(Trueness_RMS_mm),
.groups = "drop"
 ) %>% 
 pivot_wider(names_from = SKD, values_from = c(Mean_RMS, SD_RMS)) %>% 
 print()
 
# ============================================================
# 3. TWO-WAY ANOVA (Scanner × SKD) 
# ============================================================
 
# Full factorial model 
model <- aov(Trueness_RMS_mm ~ Scanner * SKD, data = trueness)
summary(model)
 
# Effect sizes (eta-squared) 
eta_squared(model, partial = TRUE) 
 
# ============================================================
# 4. POST-HOC TESTS 
# ============================================================
 
# Pairwise scanner comparisons (Tukey HSD)
TukeyHSD(model, "Scanner")
 
# Or using emmeans for more control
emm_scanner <- emmeans(model, ~ Scanner) 
pairs(emm_scanner, adjust = "tukey")
 
# Effect sizes for pairwise comparisons (Cohen's d)
eff_size(emm_scanner, sigma = sigma(model), edf = df.residual(model))
 
# ============================================================
# 5. VISUALIZATION
# ============================================================
 
# Boxplot: Trueness by Scanner
ggplot(trueness, aes(x = reorder(Scanner, Trueness_RMS_mm), y = Trueness_RMS_mm)) +
 geom_boxplot(fill = "steelblue", alpha = 0.7) + 
 geom_jitter(width = 0.2, alpha = 0.3) +
 labs( 
title = "Trueness (RMS) by Scanner", 
x = "Scanner",
y = "RMS Distance (mm)"
 ) +
 coord_flip() + 
 theme_minimal()
 
ggsave("trueness_by_scanner.png", width = 10, height = 6)
 
# Interaction plot: Scanner × SKD
ggplot(trueness, aes(x = SKD, y = Trueness_RMS_mm, color = Scanner, group = Scanner)) +
 stat_summary(fun = mean, geom = "line", size = 1) +
 stat_summary(fun = mean, geom = "point", size = 3) +
 stat_summary(fun.data = mean_se, geom = "errorbar", width = 0.2) +
 labs( 
title = "Trueness by SKD Level and Scanner",
x = "SKD (mm)", 
y = "Mean RMS Distance (mm)"
 ) +
 theme_minimal() + 
 theme(legend.position = "bottom")
 
ggsave("trueness_interaction.png", width = 10, height = 6) 
 
# Heatmap: Mean RMS by Scanner × SKD
trueness %>%
 group_by(Scanner, SKD) %>% 
 summarise(Mean_RMS = mean(Trueness_RMS_mm), .groups = "drop") %>% 
 ggplot(aes(x = SKD, y = Scanner, fill = Mean_RMS)) +
 geom_tile() +
 geom_text(aes(label = sprintf("%.3f", Mean_RMS)), color = "white", size = 3) +
 scale_fill_viridis_c(option = "plasma", direction = -1) +
 labs( 
title = "Mean Trueness RMS (mm) by Scanner and SKD",
fill = "RMS (mm)"
 ) +
 theme_minimal()
 
ggsave("trueness_heatmap.png", width = 10, height = 6)
 
# ============================================================
# 6. PRECISION ANALYSIS
# ============================================================
 
precision <- precision %>%
 mutate(
Scanner = factor(Scanner_Model),
SKD = factor(SKD_Value, ordered = TRUE) 
 )
 
# Precision by scanner 
ggplot(precision, aes(x = reorder(Scanner, Precision_MeanRMS_mm), y = Precision_MeanRMS_mm)) + 
 geom_col(fill = "darkorange", alpha = 0.8) + 
 geom_errorbar(aes(ymin = Precision_MeanRMS_mm - Precision_SD_mm,
 ymax = Precision_MeanRMS_mm + Precision_SD_mm), 
width = 0.3) +
 labs( 
title = "Precision (Pairwise RMS) by Scanner",
x = "Scanner",
y = "Mean Pairwise RMS (mm)"
 ) +
 coord_flip() + 
 theme_minimal()
 
ggsave("precision_by_scanner.png", width = 10, height = 6) 
 
# ============================================================
# 7. SAVE RESULTS
# ============================================================
 
# ANOVA results to file
sink("anova_results.txt") 
cat("=== Two-Way ANOVA: Trueness ~ Scanner * SKD ===\n\n") 
print(summary(model))
cat("\n=== Effect Sizes (Partial Eta-Squared) ===\n\n") 
print(eta_squared(model, partial = TRUE))
cat("\n=== Tukey HSD: Scanner ===\n\n")
print(TukeyHSD(model, "Scanner"))
sink()
 
cat("Analysis complete! Check output files.\n")
 
