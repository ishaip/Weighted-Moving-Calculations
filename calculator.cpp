#include "calculator.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <map>

using namespace std;

// ==================== TWMACalculator Implementation ====================

void TWMACalculator::AddNewValue(double price, long long timestamp) {
    // Add new data point to vector (no pruning - keep all data for accurate queries)
    window.push_back(DataPoint(timestamp, price));
    lastTimestamp = timestamp;
    lastPrice = price;
}

double TWMACalculator::GetCurrentTWMA(long long t) {
    if (window.empty()) {
        return 0.0;
    }

    long long windowStart = t - WINDOW_SIZE_NS;
    double weightedSum = 0.0;

    // Binary search to find the first entry >= windowStart-1 second (some buffer)
    // We need entries whose endTime > windowStart, so we can skip earlier entries
    size_t firstIdx = 0;
    for (size_t i = 0; i < window.size(); ++i) {
        if (i + 1 < window.size()) {
            if (window[i + 1].timestamp > windowStart) {
                firstIdx = i;
                break;
            }
        } else {
            firstIdx = i;
            break;
        }
    }

    // Direct vector iteration from firstIdx only
    for (size_t i = firstIdx; i < window.size(); ++i) {
        const auto& entry = window[i];
        
        // End time of this entry
        long long entryEnd;
        if (i + 1 < window.size()) {
            entryEnd = window[i + 1].timestamp;
        } else {
            entryEnd = t;
        }
        
        if (entryEnd <= windowStart) {
            continue;
        }
        
        long long startTime = std::max(entry.timestamp, windowStart);
        long long actualEndTime = std::min(entryEnd, t);
        
        if (actualEndTime > startTime) {
            double duration_ns = static_cast<double>(actualEndTime - startTime);
            weightedSum += entry.price * duration_ns;
        }
    }

    return weightedSum / WINDOW_SIZE_NS;
}

double TWMACalculator::GetCurrentTWSTD(long long t) {
    if (window.empty()) {
        return 0.0;
    }

    // First calculate TWMA
    double twma = GetCurrentTWMA(t);

    long long windowStart = t - WINDOW_SIZE_NS;
    double sumSquaredDev = 0.0;

    // Binary search to find the first entry >= windowStart-1 second
    size_t firstIdx = 0;
    for (size_t i = 0; i < window.size(); ++i) {
        if (i + 1 < window.size()) {
            if (window[i + 1].timestamp > windowStart) {
                firstIdx = i;
                break;
            }
        } else {
            firstIdx = i;
            break;
        }
    }

    // Direct vector iteration from firstIdx only
    for (size_t i = firstIdx; i < window.size(); ++i) {
        const auto& entry = window[i];
        
        // End time of this entry
        long long entryEnd;
        if (i + 1 < window.size()) {
            entryEnd = window[i + 1].timestamp;
        } else {
            entryEnd = t;
        }
        
        if (entryEnd <= windowStart) {
            continue;
        }
        
        long long startTime = std::max(entry.timestamp, windowStart);
        long long actualEndTime = std::min(entryEnd, t);
        
        if (actualEndTime > startTime) {
            double duration_ns = static_cast<double>(actualEndTime - startTime);
            double deviation = entry.price - twma;
            sumSquaredDev += duration_ns * deviation * deviation;
        }
    }

    double variance = sumSquaredDev / WINDOW_SIZE_NS;
    return std::sqrt(variance);
}

// ==================== TWMMCalculator Implementation ====================

void TWMMCalculator::PruneWindow(long long t) {
    // Remove entries older than 30 seconds
    long long windowStart = t - WINDOW_SIZE_NS;
    
    auto it = entries.begin();
    while (it != entries.end()) {
        if (it->timestamp < windowStart) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

vector<TWMMCalculator::PriceDuration> TWMMCalculator::GetPriceLevels(long long t) const {
    if (entries.empty()) {
        return {};
    }

    long long windowStart = t - WINDOW_SIZE_NS;

    // Map of price to cumulative duration (more efficient than iterating multiple times)
    map<double, double> priceDurations;
    vector<DataPoint> windowEntries;
    
    // Filter entries in window
    for (const auto& entry : entries) {
        if (entry.timestamp < windowStart) {
            continue;
        }
        windowEntries.push_back(entry);
    }

    if (windowEntries.empty()) {
        return {};
    }

    // Calculate durations for each entry
    for (size_t i = 0; i < windowEntries.size(); ++i) {
        long long entryStart = windowEntries[i].timestamp;
        long long entryEnd;

        if (i + 1 < windowEntries.size()) {
            // End at start of next entry
            entryEnd = windowEntries[i + 1].timestamp;
        } else {
            // End at query time
            entryEnd = t;
        }

        // Clip to window boundaries
        entryStart = std::max(entryStart, windowStart);
        entryEnd = std::min(entryEnd, t);

        if (entryEnd > entryStart) {
            double duration_ns = static_cast<double>(entryEnd - entryStart);
            priceDurations[windowEntries[i].price] += duration_ns;
        }
    }

    // Convert map to sorted vector
    vector<PriceDuration> result;
    double cumulative = 0.0;

    for (const auto& [price, duration] : priceDurations) {
        cumulative += duration;
        result.push_back({price, cumulative});
    }

    return result;
}

void TWMMCalculator::AddNewValue(double price, long long timestamp) {
    entries.push_back(DataPoint(timestamp, price));
    // No pruning here - keep all data for accurate queries
}

double TWMMCalculator::GetCurrentTWMM(long long t) {
    // No pruning - get price levels filters by window automatically
    if (entries.empty()) {
        return 0.0;
    }

    auto priceLevels = GetPriceLevels(t);

    if (priceLevels.empty()) {
        return 0.0;
    }

    // Total duration in window
    double totalDuration = priceLevels.back().cumulativeDuration;

    // Find the median: price where cumulative >= 15s and (total - cumulative) >= 15s
    for (size_t i = 0; i < priceLevels.size(); ++i) {
        double cumulativeUp = priceLevels[i].cumulativeDuration;
        double cumulativeDown = totalDuration - (i > 0 ? priceLevels[i - 1].cumulativeDuration : 0.0);

        if (cumulativeUp >= HALF_WINDOW_NS && cumulativeDown >= HALF_WINDOW_NS) {
            return priceLevels[i].price;
        }
    }

    // If no exact median found, return the middle price by cumulative duration
    double targetDuration = totalDuration / 2.0;
    for (size_t i = 0; i < priceLevels.size(); ++i) {
        if (priceLevels[i].cumulativeDuration >= targetDuration) {
            return priceLevels[i].price;
        }
    }

    return priceLevels.back().price;
}

// ==================== FileProcessor Implementation ====================

pair<vector<long long>, vector<double>> 
FileProcessor::ReadSecurityCSV(const string& filename) {
    vector<long long> timestamps;
    vector<double> prices;

    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << endl;
        return {timestamps, prices};
    }

    string line;
    
    // Skip header
    getline(file, line);

    // Read data in buffers
    vector<string> buffer;
    while (std::getline(file, line)) {
        buffer.push_back(line);

        if (buffer.size() >= BUFFER_SIZE) {
            // Process buffer
            for (const auto& bufLine : buffer) {
                stringstream ss(bufLine);
                double timestampDouble;
                double price;
                char comma;

                if (ss >> timestampDouble >> comma >> price) {
                    // Convert double timestamp to long long (nanoseconds)
                    long long timestamp = static_cast<long long>(timestampDouble);
                    timestamps.push_back(timestamp);
                    prices.push_back(price);
                }
            }
            buffer.clear();
        }
    }

    // Process remaining buffer
    for (const auto& bufLine : buffer) {
        stringstream ss(bufLine);
        double timestampDouble;
        double price;
        char comma;

        if (ss >> timestampDouble >> comma >> price) {
            // Convert double timestamp to long long (nanoseconds)
            long long timestamp = static_cast<long long>(timestampDouble);
            timestamps.push_back(timestamp);
            prices.push_back(price);
        }
    }

    file.close();
    cout << "Loaded " << timestamps.size() << " data points from " << filename << endl;
    return {timestamps, prices};
}

void FileProcessor::WriteCSV(const string& filename,
                             const vector<long long>& timestamps,
                             const vector<double>& values,
                             const string& valueHeader) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << " for writing" << endl;
        return;
    }

    // Write header
    file << "Time," << valueHeader << "\n";

    // Write data in buffers
    vector<string> buffer;
    for (size_t i = 0; i < timestamps.size(); ++i) {
        stringstream ss;
        ss << fixed << setprecision(6);
        ss << timestamps[i] << "," << values[i];
        buffer.push_back(ss.str());

        if (buffer.size() >= BUFFER_SIZE) {
            // Flush buffer
            for (const auto& line : buffer) {
                file << line << "\n";
            }
            buffer.clear();
        }
    }

    // Flush remaining buffer
    for (const auto& line : buffer) {
        file << line << "\n";
    }

    file.close();
    cout << "Wrote " << timestamps.size() << " rows to " << filename << endl;
}

void FileProcessor::WriteCSV(const string& filename,
                             const vector<long long>& timestamps,
                             const vector<double>& values1,
                             const vector<double>& values2,
                             const string& header1,
                             const string& header2) {
    ofstream file(filename);
    if (!file.is_open()) {
        cerr << "Error: Cannot open file " << filename << " for writing" << endl;
        return;
    }

    // Write header
    file << "Time," << header1 << "," << header2 << "\n";

    // Write data in buffers
    vector<string> buffer;
    for (size_t i = 0; i < timestamps.size(); ++i) {
        stringstream ss;
        ss << fixed << setprecision(6);
        ss << timestamps[i] << "," << values1[i] << "," << values2[i];
        buffer.push_back(ss.str());

        if (buffer.size() >= BUFFER_SIZE) {
            // Flush buffer
            for (const auto& line : buffer) {
                file << line << "\n";
            }
            buffer.clear();
        }
    }

    // Flush remaining buffer
    for (const auto& line : buffer) {
        file << line << "\n";
    }

    file.close();
    std::cout << "Wrote " << timestamps.size() << " rows to " << filename << std::endl;
}
