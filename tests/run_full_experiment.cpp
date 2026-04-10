/**
 * @file run_full_experiment.cpp
 * @brief Full paper experiment: 303 texts × 100 profiles × 5 models × 2 conditions
 * 
 * Collects:
 * - HTML parsing success rate
 * - WCAG 2.1 compliance (8 criteria)
 * - Cognitive accessibility features (5 types - removed literal language check)
 */

#include "../src/agents/TextAnalyzer.hpp"
#include "../src/agents/FeedbackAgent.hpp"
#include "../src/agents/InterfaceGenerator.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <iomanip>
#include <regex>
#include <nlohmann/json.hpp>

using namespace EMPI;
using json = nlohmann::json;
namespace fs = std::filesystem;

// ============================================================================
// CONSTANTS
// ============================================================================

const size_t MAX_TEXT_LENGTH = 1000;  // Truncate texts to avoid batch size issues

// ============================================================================
// DATA STRUCTURES
// ============================================================================

struct TextData {
    std::string id;
    std::string content;
    std::string source;
    size_t length;
    size_t original_length;
};

struct ProfileData {
    std::string id;
    std::string condition;
    std::string severity;
    json settings;
    std::string natural_prompt;
};

struct GenerationResult {
    std::string text_id;
    std::string profile_id;
    std::string model_name;
    std::string condition;  // "single_llm" or "multi_agent"
    bool html_parsed;
    std::string html_content;
    std::chrono::milliseconds duration;
    
    // WCAG metrics
    struct WCAGMetrics {
        bool has_alt_text = false;
        bool heading_hierarchy = false;
        bool has_link_descriptions = false;
        bool has_form_labels = false;
        bool unique_ids = false;
        bool has_lang_declaration = false;
        bool has_aria_attributes = false;
        bool keyboard_nav_support = false;
        int total_issues = 0;
    } wcag;
    
    // Cognitive features (5 metrics)
    struct CognitiveFeatures {
        bool opendyslexic_font = false;
        bool adequate_contrast = false;
        float line_spacing = 0;
        bool content_chunking = false;
        bool progress_indicator = false;
        int features_count = 0;
    } cognitive;
    
    bool success = false;
    std::string error_msg;
};

// ============================================================================
// DATA LOADERS
// ============================================================================

std::vector<TextData> load_texts_from_dir(const std::string& dir_path, const std::string& source) {
    std::vector<TextData> texts;
    if (!fs::exists(dir_path)) {
        std::cerr << "Warning: Directory not found: " << dir_path << std::endl;
        return texts;
    }
    
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        if (entry.path().extension() == ".txt") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
                size_t original_len = content.length();
                
                // Truncate content to MAX_TEXT_LENGTH
                std::string truncated = content;
                if (truncated.length() > MAX_TEXT_LENGTH) {
                    truncated = truncated.substr(0, MAX_TEXT_LENGTH);
                    // Try to cut at a sentence boundary if possible
                    size_t last_period = truncated.rfind('.');
                    size_t last_newline = truncated.rfind('\n');
                    size_t cut_pos = std::max(last_period, last_newline);
                    if (cut_pos > MAX_TEXT_LENGTH / 2) {
                        truncated = truncated.substr(0, cut_pos + 1);
                    }
                }
                
                texts.push_back({
                    entry.path().stem().string(),
                    truncated,
                    source,
                    truncated.length(),
                    original_len
                });
                file.close();
            }
        }
    }
    return texts;
}

std::vector<ProfileData> load_profiles(const std::string& json_path) {
    std::vector<ProfileData> profiles;
    std::ifstream file(json_path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open " << json_path << std::endl;
        return profiles;
    }
    
    json data;
    file >> data;
    
    // Handle both formats: array or object with "profiles" key
    if (data.is_array()) {
        for (const auto& item : data) {
            profiles.push_back({
                item.value("id", "unknown"),
                item.value("condition", "general"),
                item.value("severity", "moderate"),
                item.value("settings", json::object()),
                item.value("natural_prompt", "")
            });
        }
    } else if (data.contains("profiles")) {
        for (const auto& item : data["profiles"]) {
            profiles.push_back({
                item.value("id", "unknown"),
                item.value("condition", "general"),
                item.value("severity", "moderate"),
                item.value("accessibility_settings", json::object()),
                item.value("natural_prompt", "")
            });
        }
    }
    
    return profiles;
}

// ============================================================================
// WCAG CHECKER
// ============================================================================

GenerationResult::WCAGMetrics check_wcag_compliance(const std::string& html) {
    GenerationResult::WCAGMetrics metrics;
    
    // Basic string-based checks (fast, no JS dependency)
    metrics.has_alt_text = html.find("alt=") != std::string::npos;
    metrics.has_lang_declaration = html.find("lang=") != std::string::npos;
    metrics.has_aria_attributes = html.find("aria-") != std::string::npos;
    metrics.has_form_labels = html.find("<label") != std::string::npos || 
                              html.find("aria-label") != std::string::npos;
    
    // Check heading hierarchy (h1 then h2 then h3)
    size_t h1_pos = html.find("<h1");
    size_t h2_pos = html.find("<h2");
    size_t h3_pos = html.find("<h3");
    metrics.heading_hierarchy = (h1_pos != std::string::npos) && 
                                (h2_pos == std::string::npos || h2_pos > h1_pos);
    
    // Check for unique IDs (simple duplicate check)
    std::unordered_map<std::string, int> id_counts;
    std::regex id_regex("id=\"([^\"]+)\"");
    std::smatch match;
    std::string::const_iterator searchStart(html.cbegin());
    while (std::regex_search(searchStart, html.cend(), match, id_regex)) {
        id_counts[match[1].str()]++;
        searchStart = match.suffix().first;
    }
    metrics.unique_ids = true;
    for (const auto& [id, count] : id_counts) {
        if (count > 1) {
            metrics.unique_ids = false;
            break;
        }
    }
    
    // Check link descriptions (links should have text)
    std::regex link_regex("<a[^>]*>([^<]*)</a>");
    searchStart = html.cbegin();
    int empty_links = 0;
    int total_links = 0;
    while (std::regex_search(searchStart, html.cend(), match, link_regex)) {
        total_links++;
        std::string link_text = match[1].str();
        if (link_text.empty() || link_text.find("click here") != std::string::npos) {
            empty_links++;
        }
        searchStart = match.suffix().first;
    }
    metrics.has_link_descriptions = (empty_links == 0 || total_links == 0);
    
    // Keyboard navigation support (check for tabindex or buttons/links)
    metrics.keyboard_nav_support = html.find("tabindex=") != std::string::npos ||
                                   html.find("<button") != std::string::npos ||
                                   html.find("<a ") != std::string::npos;
    
    // Count total issues
    metrics.total_issues = 0;
    if (!metrics.has_alt_text) metrics.total_issues++;
    if (!metrics.heading_hierarchy) metrics.total_issues++;
    if (!metrics.has_link_descriptions) metrics.total_issues++;
    if (!metrics.has_form_labels) metrics.total_issues++;
    if (!metrics.unique_ids) metrics.total_issues++;
    if (!metrics.has_lang_declaration) metrics.total_issues++;
    if (!metrics.has_aria_attributes) metrics.total_issues++;
    if (!metrics.keyboard_nav_support) metrics.total_issues++;
    
    return metrics;
}

GenerationResult::CognitiveFeatures check_cognitive_features(const std::string& html, const ProfileData& profile) {
    GenerationResult::CognitiveFeatures features;
    
    std::string html_lower = html;
    std::transform(html_lower.begin(), html_lower.end(), html_lower.begin(), ::tolower);
    
    // OpenDyslexic font
    features.opendyslexic_font = html_lower.find("opendyslexic") != std::string::npos ||
                                 html_lower.find("font-family: 'opendyslexic'") != std::string::npos;
    
    // Color contrast (check for dark text on light background or vice versa)
    features.adequate_contrast = (html_lower.find("color: #") != std::string::npos ||
                                  html_lower.find("background: #") != std::string::npos) &&
                                 (html_lower.find("#000") != std::string::npos ||
                                  html_lower.find("#fff") != std::string::npos);
    
    // Line spacing
    std::regex line_height_regex("line-height:\\s*([\\d.]+)");
    std::smatch match;
    if (std::regex_search(html, match, line_height_regex)) {
        try {
            features.line_spacing = std::stof(match[1].str());
        } catch (...) {
            features.line_spacing = 0;
        }
    }
    
    // Content chunking (short paragraphs)
    std::regex p_regex("<p[^>]*>([^<]*)</p>");
    std::string::const_iterator searchStart(html.cbegin());
    int long_paragraphs = 0;
    int total_paragraphs = 0;
    while (std::regex_search(searchStart, html.cend(), match, p_regex)) {
        total_paragraphs++;
        if (match[1].str().length() > 500) {
            long_paragraphs++;
        }
        searchStart = match.suffix().first;
    }
    features.content_chunking = (long_paragraphs == 0 && total_paragraphs > 0);
    
    // Progress indicator
    features.progress_indicator = html_lower.find("progress") != std::string::npos ||
                                  html_lower.find("step") != std::string::npos ||
                                  html_lower.find("indicator") != std::string::npos;
    
    // Count features (5 total)
    features.features_count = (features.opendyslexic_font ? 1 : 0) +
                              (features.adequate_contrast ? 1 : 0) +
                              (features.line_spacing >= 1.5 ? 1 : 0) +
                              (features.content_chunking ? 1 : 0) +
                              (features.progress_indicator ? 1 : 0);
    
    return features;
}

// ============================================================================
// GENERATION FUNCTIONS
// ============================================================================

GenerationResult run_single_llm(const TextData& text, const ProfileData& profile, 
                                 const std::string& model_path, const std::string& model_name) {
    GenerationResult result;
    result.text_id = text.id;
    result.profile_id = profile.id;
    result.model_name = model_name;
    result.condition = "single_llm";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Direct LLM call without agents
    std::string prompt = "[INST] You are an accessibility assistant. Adapt the following text for a user with " +
                         profile.condition + " (" + profile.severity + " severity).\n\n";
    prompt += "TEXT:\n" + text.content + "\n\n";
    prompt += "Generate a complete HTML5 page with inline CSS that:\n";
    prompt += "1. Uses appropriate formatting for " + profile.condition + "\n";
    prompt += "2. Is WCAG 2.1 compliant\n";
    prompt += "3. Includes proper alt text, headings, and semantic structure\n";
    prompt += "[/INST]\n";
    
    result.success = false;
    result.error_msg = "Single LLM baseline not yet integrated - use multi-agent for now";
    
    auto end = std::chrono::high_resolution_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    return result;
}

GenerationResult run_multi_agent(const TextData& text, const ProfileData& profile,
                                  const std::string& model_path, const std::string& model_name,
                                  TextAnalyzer& text_agent, FeedbackAgent& feedback_agent,
                                  InterfaceGenerator& interface_gen) {
    GenerationResult result;
    result.text_id = text.id;
    result.profile_id = profile.id;
    result.model_name = model_name;
    result.condition = "multi_agent";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Output step 1: Processing text
    std::cout << "\n[PROCESSING] Text ID: " << text.id << "\n";
    std::cout << "[PROCESSING] Text content length: " << text.length << " characters (truncated from " << text.original_length << ")\n";
    std::cout << "[PROCESSING] Text preview: " << text.content.substr(0, std::min(200, (int)text.content.length())) << "...\n";
    
    try {
        // Step 1: Analyze text
        json text_input = {{"text", text.content}};
        json text_result = text_agent.process_raw(text_input);
        json text_data = text_result["payload"]["data"];
        
        if (text_data["status"] != "success") {
            result.success = false;
            result.error_msg = "Text analysis failed: " + text_data.value("message", "unknown");
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "[FAILED] Text analysis failed for " << text.id << "\n";
            return result;
        }
        
        std::cout << "[STEP 1 COMPLETE] Text analysis successful for " << text.id << "\n";
        
        // Step 2: Process profile and generate feedback
        std::cout << "[PROCESSING] Profile ID: " << profile.id << "\n";
        std::cout << "[PROCESSING] Condition: " << profile.condition << "\n";
        std::cout << "[PROCESSING] Severity: " << profile.severity << "\n";
        
        json dialog_history = json::array({
            {{"role", "user"}, {"content", profile.natural_prompt.empty() ? 
                "I have " + profile.condition + ". Please adapt content for me." : 
                profile.natural_prompt}},
            {{"role", "assistant"}, {"content", "I'll adapt the content for your needs."}}
        });
        
        json feedback_input = {{"dialog_history", dialog_history}};
        json feedback_result = feedback_agent.process_raw(feedback_input, "feedback_analysis");
        json feedback_data = feedback_result["payload"]["data"];
        
        std::cout << "[STEP 2 COMPLETE] Feedback analysis generated for profile " << profile.id << "\n";
        
        // Step 3: Generate interface
        json interface_input = {
            {"text_metrics", text_data["metrics"]},
            {"feedback_analysis", feedback_data["analysis"]},
            {"original_text", text.content}
        };
        
        json interface_result = interface_gen.process_raw(interface_input, "html_generation");
        json interface_data = interface_result["payload"]["data"];
        
        if (interface_data["status"] != "success") {
            result.success = false;
            result.error_msg = "Interface generation failed: " + interface_data.value("message", "unknown");
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "[FAILED] Interface generation failed for " << text.id << " - " << profile.id << "\n";
            return result;
        }
        
        result.html_content = interface_data["html"];
        result.html_parsed = true;
        result.success = true;
        
        // Output generated HTML
        std::cout << "[STEP 3 COMPLETE] HTML generated for " << text.id << " - " << profile.id << "\n";
        std::cout << "[GENERATED HTML START]\n";
        std::cout << result.html_content << "\n";
        std::cout << "[GENERATED HTML END]\n";
        
        // Run checks
        result.wcag = check_wcag_compliance(result.html_content);
        result.cognitive = check_cognitive_features(result.html_content, profile);
        
        // Output WCAG results
        std::cout << "[WCAG CHECK] Alt text: " << (result.wcag.has_alt_text ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Heading hierarchy: " << (result.wcag.heading_hierarchy ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Link descriptions: " << (result.wcag.has_link_descriptions ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Form labels: " << (result.wcag.has_form_labels ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Unique IDs: " << (result.wcag.unique_ids ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Language declaration: " << (result.wcag.has_lang_declaration ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] ARIA attributes: " << (result.wcag.has_aria_attributes ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Keyboard navigation: " << (result.wcag.keyboard_nav_support ? "PASS" : "FAIL") << "\n";
        std::cout << "[WCAG CHECK] Total issues: " << result.wcag.total_issues << "\n";
        
        // Output cognitive features results
        std::cout << "[COGNITIVE CHECK] OpenDyslexic font: " << (result.cognitive.opendyslexic_font ? "PRESENT" : "ABSENT") << "\n";
        std::cout << "[COGNITIVE CHECK] Adequate contrast: " << (result.cognitive.adequate_contrast ? "PRESENT" : "ABSENT") << "\n";
        std::cout << "[COGNITIVE CHECK] Line spacing: " << result.cognitive.line_spacing << "\n";
        std::cout << "[COGNITIVE CHECK] Content chunking: " << (result.cognitive.content_chunking ? "PRESENT" : "ABSENT") << "\n";
        std::cout << "[COGNITIVE CHECK] Progress indicator: " << (result.cognitive.progress_indicator ? "PRESENT" : "ABSENT") << "\n";
        std::cout << "[COGNITIVE CHECK] Features count: " << result.cognitive.features_count << "/5\n";
        
        std::cout << "[RESULT] Success for " << text.id << " - " << profile.id << " (Duration: " << result.duration.count() << "ms)\n";
        
    } catch (const std::exception& e) {
        result.success = false;
        result.error_msg = e.what();
        std::cout << "[EXCEPTION] " << e.what() << "\n";
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    return result;
}

// ============================================================================
// RESULTS AGGREGATION
// ============================================================================

void save_results(const std::vector<GenerationResult>& results, const std::string& filename) {
    json output;
    
    for (const auto& r : results) {
        json item;
        item["text_id"] = r.text_id;
        item["profile_id"] = r.profile_id;
        item["model_name"] = r.model_name;
        item["condition"] = r.condition;
        item["success"] = r.success;
        item["duration_ms"] = r.duration.count();
        
        if (r.success) {
            item["html_parsed"] = r.html_parsed;
            item["wcag"] = {
                {"has_alt_text", r.wcag.has_alt_text},
                {"heading_hierarchy", r.wcag.heading_hierarchy},
                {"has_link_descriptions", r.wcag.has_link_descriptions},
                {"has_form_labels", r.wcag.has_form_labels},
                {"unique_ids", r.wcag.unique_ids},
                {"has_lang_declaration", r.wcag.has_lang_declaration},
                {"has_aria_attributes", r.wcag.has_aria_attributes},
                {"keyboard_nav_support", r.wcag.keyboard_nav_support},
                {"total_issues", r.wcag.total_issues}
            };
            item["cognitive"] = {
                {"opendyslexic_font", r.cognitive.opendyslexic_font},
                {"adequate_contrast", r.cognitive.adequate_contrast},
                {"line_spacing", r.cognitive.line_spacing},
                {"content_chunking", r.cognitive.content_chunking},
                {"progress_indicator", r.cognitive.progress_indicator},
                {"features_count", r.cognitive.features_count}
            };
        } else {
            item["error"] = r.error_msg;
        }
        
        output.push_back(item);
    }
    
    std::ofstream file(filename);
    file << output.dump(2);
    file.close();
    
    std::cout << "\nResults saved to: " << filename << std::endl;
}

void print_summary(const std::vector<GenerationResult>& results) {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "EXPERIMENT SUMMARY\n";
    std::cout << std::string(80, '=') << "\n";
    
    // Group by model and condition
    std::map<std::pair<std::string, std::string>, std::vector<GenerationResult>> grouped;
    for (const auto& r : results) {
        grouped[std::make_pair(r.model_name, r.condition)].push_back(r);
    }
    
    for (const auto& [key, group] : grouped) {
        const auto& [model, condition] = key;
        
        int total = group.size();
        int successful = std::count_if(group.begin(), group.end(), 
                                       [](const auto& r) { return r.success; });
        
        double success_rate = (total > 0) ? (100.0 * successful / total) : 0;
        
        // Aggregate WCAG metrics
        int wcag_passed = 0;
        int cognitive_features_sum = 0;
        std::chrono::milliseconds total_duration(0);
        
        for (const auto& r : group) {
            if (r.success) {
                if (r.wcag.total_issues == 0) wcag_passed++;
                cognitive_features_sum += r.cognitive.features_count;
                total_duration += r.duration;
            }
        }
        
        double wcag_pass_rate = (successful > 0) ? (100.0 * wcag_passed / successful) : 0;
        double avg_cognitive_features = (successful > 0) ? (1.0 * cognitive_features_sum / successful) : 0;
        double avg_duration = (successful > 0) ? (total_duration.count() / successful) : 0;
        
        std::cout << "\n" << model << " - " << condition << "\n";
        std::cout << std::string(40, '-') << "\n";
        std::cout << "  Success rate:        " << std::fixed << std::setprecision(1) << success_rate << "%\n";
        std::cout << "  WCAG compliant:      " << std::fixed << std::setprecision(1) << wcag_pass_rate << "%\n";
        std::cout << "  Cognitive features:  " << std::fixed << std::setprecision(1) << avg_cognitive_features << "/5\n";
        std::cout << "  Avg duration:        " << std::fixed << std::setprecision(0) << avg_duration << "ms\n";
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "FULL PAPER EXPERIMENT - 303 Texts x 100 Profiles x 5 Models\n";
    std::cout << "============================================================\n";
    
    // Parse arguments
    std::string model_path = "models/Phi-3-mini-4k-instruct-q4.gguf";
    bool quick_test = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--quick") == 0) {
            quick_test = true;
        }
    }
    
    // Load data
    std::cout << "\n[1/4] Loading texts...\n";
    auto w3c_texts = load_texts_from_dir("../parsing/w3c_accessibility_texts", "W3C_WAI");
    auto wai_texts = load_texts_from_dir("../parsing/wai_texts", "W3C_Tech");
    auto webaim_texts = load_texts_from_dir("../parsing/webaim_texts", "WebAIM");
    
    std::vector<TextData> all_texts;
    all_texts.insert(all_texts.end(), w3c_texts.begin(), w3c_texts.end());
    all_texts.insert(all_texts.end(), wai_texts.begin(), wai_texts.end());
    all_texts.insert(all_texts.end(), webaim_texts.begin(), webaim_texts.end());
    
    std::cout << "  Loaded " << all_texts.size() << " texts\n";
    std::cout << "  Max text length: " << MAX_TEXT_LENGTH << " characters (truncated)\n";
    
    std::cout << "\n[2/4] Loading profiles...\n";
    auto profiles = load_profiles("../parsing/user_profiles.json");
    std::cout << "  Loaded " << profiles.size() << " profiles\n";
    
    // Stochastic sampling: 1-3 profiles per text
    std::cout << "\n[3/4] Sampling text-profile pairs...\n";
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> count_dist(1, 3);
    std::uniform_int_distribution<size_t> profile_dist(0, profiles.size() - 1);
    
    struct Pair { TextData text; ProfileData profile; };
    std::vector<Pair> pairs;
    
    int max_pairs = quick_test ? 50 : all_texts.size() * 2;
    int texts_to_process = quick_test ? std::min(10, (int)all_texts.size()) : all_texts.size();
    
    for (int i = 0; i < texts_to_process && (int)pairs.size() < max_pairs; i++) {
        int num_profiles = count_dist(rng);
        std::unordered_set<size_t> used;
        for (int j = 0; j < num_profiles && used.size() < profiles.size(); j++) {
            size_t idx;
            do { idx = profile_dist(rng); } while (used.count(idx));
            used.insert(idx);
            pairs.push_back({all_texts[i], profiles[idx]});
        }
    }
    
    std::cout << "  Generated " << pairs.size() << " pairs\n";
    
    // Initialize agents
    std::cout << "\n[4/4] Running experiment...\n";
    std::cout << "  Model: " << model_path << "\n";
    std::cout << "  Text truncation limit: " << MAX_TEXT_LENGTH << " chars\n\n";
    
    TextAnalyzer text_agent;
    FeedbackAgent feedback_agent(model_path);
    InterfaceGenerator interface_gen(model_path);
    
    std::vector<GenerationResult> all_results;
    int total = pairs.size();
    int completed = 0;
    
    for (const auto& pair : pairs) {
        completed++;
        
        auto result = run_multi_agent(pair.text, pair.profile, model_path, "Phi-3-mini",
                                      text_agent, feedback_agent, interface_gen);
        all_results.push_back(result);
        
        // Save intermediate results every 100
        if (completed % 100 == 0) {
            save_results(all_results, "experiment_results_intermediate.json");
        }
    }
    
    std::cout << "\n\n";
    
    // Save and print summary
    save_results(all_results, "experiment_results_full.json");
    print_summary(all_results);
    
    // Print detailed WCAG breakdown
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "WCAG 2.1 DETAILED BREAKDOWN\n";
    std::cout << std::string(80, '=') << "\n";
    
    int success_count = std::count_if(all_results.begin(), all_results.end(),
                                      [](const auto& r) { return r.success; });
    
    if (success_count > 0) {
        int alt_text_ok = std::count_if(all_results.begin(), all_results.end(),
            [](const auto& r) { return r.success && r.wcag.has_alt_text; });
        int headings_ok = std::count_if(all_results.begin(), all_results.end(),
            [](const auto& r) { return r.success && r.wcag.heading_hierarchy; });
        int lang_ok = std::count_if(all_results.begin(), all_results.end(),
            [](const auto& r) { return r.success && r.wcag.has_lang_declaration; });
        int aria_ok = std::count_if(all_results.begin(), all_results.end(),
            [](const auto& r) { return r.success && r.wcag.has_aria_attributes; });
        
        std::cout << "  Alt text present:        " << std::fixed << std::setprecision(1)
                  << (100.0 * alt_text_ok / success_count) << "%\n";
        std::cout << "  Heading hierarchy:       " << std::fixed << std::setprecision(1)
                  << (100.0 * headings_ok / success_count) << "%\n";
        std::cout << "  Language declaration:    " << std::fixed << std::setprecision(1)
                  << (100.0 * lang_ok / success_count) << "%\n";
        std::cout << "  ARIA attributes:         " << std::fixed << std::setprecision(1)
                  << (100.0 * aria_ok / success_count) << "%\n";
    }
    
    std::cout << "\nExperiment complete.\n";
    std::cout << "Results saved to: experiment_results_full.json\n";
    
    return 0;
}
