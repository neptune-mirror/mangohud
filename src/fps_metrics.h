#pragma once
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <mesa/util/os_time.h>
#include <numeric>
#include <mutex>
#include <algorithm>
#include <condition_variable>
#include <stdexcept>

struct metric_t {
    std::string name;
    float value;
    std::string display_name;
};

class fpsMetrics {
    private:
        std::vector<std::pair<uint64_t, float>> fps_stats;
        std::thread thread;
        std::mutex mtx;
        std::condition_variable cv;
        bool run = false;
        bool thread_init = false;
        bool terminate = false;

        void calculate(){
            thread_init = true;
            while (true){
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [this] { return run; });

                if (terminate)
                    break;

                std::vector<float> sorted_values;
                for (const auto& p : fps_stats)
                    sorted_values.push_back(p.second);

                std::sort(sorted_values.begin(), sorted_values.end());

                auto it = metrics.begin();
                while (it != metrics.end()) {
                    if (it->name == "AVG") {
                        it->display_name = it->name;
                        if (!fps_stats.empty()) {
                            float sum = std::accumulate(fps_stats.begin(), fps_stats.end(), 0.0f,
                                                        [](float acc, const std::pair<uint64_t, float>& p) {
                                                            return acc + p.second;
                                                        });
                            it->value = sum / fps_stats.size();
                            ++it;
                        }
                    } else {
                        try {
                            float val = std::stof(it->name);
                            if (val <= 0 || val >= 1 ) {
                                SPDLOG_DEBUG("Failed to use fps metric, it's out of range {}", it->name);
                                it = metrics.erase(it);
                                break;
                            }
                            float multiplied_val = val * 100;
                            std::ostringstream stream;
                            if (multiplied_val == static_cast<int>(multiplied_val)) {
                                stream << std::fixed << std::setprecision(0) << multiplied_val << "%";
                            } else {
                                stream << std::fixed << std::setprecision(1) << multiplied_val << "%";
                            }
                            it->display_name = stream.str();
                            uint64_t idx = val * sorted_values.size() - 1;
                            it->value = sorted_values[idx];
                            ++it;
                        } catch (const std::invalid_argument& e) {
                            SPDLOG_DEBUG("Failed to use fps metric value {}", it->name);
                            it = metrics.erase(it);
                        }
                    }
                }

                run = false;
            }
        }

    public:
        std::vector<metric_t> metrics;

        fpsMetrics(std::vector<std::string> values){
            // capitalize string
            for (auto& val : values){
                for(char& c : val) {
                    c = std::toupper(static_cast<unsigned char>(c));
                }

                metrics.push_back({val, 0.0f});
            }

            if (!thread_init){
                thread = std::thread(&fpsMetrics::calculate, this);
            }
        };

        void update(uint64_t now, double fps){
            fps_stats.push_back({now, fps});
            // Calculate the cut-off nanotime (10 minutes ago)
            uint64_t ten_minutes_ns = 600000000000ULL; // 10 minutes in nanoseconds
            uint64_t cutoff_time_ns = os_time_get_nano() - ten_minutes_ns;

            // Removing elements older than 10 minutes
            fps_stats.erase(std::remove_if(fps_stats.begin(), fps_stats.end(),
                                            [cutoff_time_ns](const std::pair<uint64_t, float>& p) {
                                                return p.first < cutoff_time_ns;
                                            }),
                            fps_stats.end());
        };

        void update_thread(){
            {
                std::lock_guard<std::mutex> lock(mtx);
                run = true;
            }
            cv.notify_one();
        }

        ~fpsMetrics(){
            terminate = true;
            {
                std::lock_guard<std::mutex> lock(mtx);
                run = true;
            }
            cv.notify_one();
            thread.join();
        }
};

extern std::unique_ptr<fpsMetrics> fpsmetrics;