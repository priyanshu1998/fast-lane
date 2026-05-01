#include "fast_lane/shared_memory.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

bool init_imgui(GLFWwindow*& window, const char* title)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window = glfwCreateWindow(960, 640, title, nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    return true;
}

void shutdown_imgui(GLFWwindow* window)
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

std::vector<std::uint8_t> make_payload(std::uint64_t tick, int value_count, std::uint64_t base)
{
    const int count = std::max(1, value_count);
    std::vector<std::uint64_t> values;
    values.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        values.push_back(base + (tick * 1000ULL) + static_cast<std::uint64_t>(index));
    }
    return fast_lane::encode_u64_sequence(values);
}

void draw_payload_preview(const std::vector<std::uint8_t>& payload)
{
    const auto values = fast_lane::decode_u64_sequence(payload);
    if (ImGui::BeginTable("payload-preview", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Index");
        ImGui::TableSetupColumn("uint64");
        ImGui::TableSetupColumn("Hex");
        ImGui::TableSetupColumn("Bytes");
        ImGui::TableHeadersRow();

        const std::size_t limit = std::min<std::size_t>(values.size(), 16);
        for (std::size_t row = 0; row < limit; ++row) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", row);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(values[row]));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("0x%016llx", static_cast<unsigned long long>(values[row]));
            ImGui::TableSetColumnIndex(3);
            const auto* byte_view = reinterpret_cast<const std::uint8_t*>(&values[row]);
            ImGui::Text("%02x %02x %02x %02x ...", byte_view[0], byte_view[1], byte_view[2], byte_view[3]);
        }
        ImGui::EndTable();
    }
}

} // namespace

int main()
{
    GLFWwindow* window = nullptr;
    if (!init_imgui(window, "Fast Lane Leader")) {
        return 1;
    }

    std::array<char, 128> channel_name{};
    std::snprintf(channel_name.data(), channel_name.size(), "%s", "fast_lane_demo");

    int capacity = 65536;
    int value_count = 16;
    int interval_ms = 500;
    std::uint64_t base_value = 0;
    std::uint64_t tick = 0;
    bool auto_publish = false;
    bool show_demo = false;

    std::optional<fast_lane::SharedMemoryChannel> channel;
    std::vector<std::uint8_t> last_payload;
    std::string status = "Create a leader channel to begin publishing.";
    auto next_publish = Clock::now();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
        ImGui::Begin("Fast Lane Leader");

        ImGui::InputText("Channel", channel_name.data(), channel_name.size());
        ImGui::InputInt("Capacity bytes", &capacity);
        ImGui::InputInt("uint64 values", &value_count);
        ImGui::InputScalar("Base value", ImGuiDataType_U64, &base_value);
        ImGui::InputInt("Auto publish interval ms", &interval_ms);

        capacity = std::max(capacity, 8);
        value_count = std::max(value_count, 1);
        interval_ms = std::max(interval_ms, 10);

        if (!channel.has_value()) {
            if (ImGui::Button("Create Leader")) {
                try {
                    channel.emplace(fast_lane::SharedMemoryChannel::create_leader(
                        channel_name.data(),
                        static_cast<std::size_t>(capacity)));
                    tick = 0;
                    auto_publish = false;
                    status = "Leader channel created.";
                } catch (const std::exception& error) {
                    status = std::string("Create failed: ") + error.what();
                }
            }
        } else {
            if (ImGui::Button("Close Leader")) {
                channel.reset();
                auto_publish = false;
                status = "Leader channel closed.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Publish Once")) {
                try {
                    ++tick;
                    last_payload = make_payload(tick, value_count, base_value);
                    channel->publish(last_payload);
                    status = "Published one payload.";
                } catch (const std::exception& error) {
                    status = std::string("Publish failed: ") + error.what();
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("Auto Publish", &auto_publish);
        }

        if (channel.has_value() && auto_publish && Clock::now() >= next_publish) {
            try {
                ++tick;
                last_payload = make_payload(tick, value_count, base_value);
                channel->publish(last_payload);
                status = "Auto-published payload.";
            } catch (const std::exception& error) {
                status = std::string("Auto publish failed: ") + error.what();
                auto_publish = false;
            }
            next_publish = Clock::now() + std::chrono::milliseconds(interval_ms);
        }

        ImGui::Separator();
        ImGui::Text("State: %s", channel.has_value() ? "leader active" : "not created");
        ImGui::Text("Published ticks: %llu", static_cast<unsigned long long>(tick));
        ImGui::Text("Last payload bytes: %zu", last_payload.size());
        ImGui::TextWrapped("Status: %s", status.c_str());

        if (!last_payload.empty()) {
            ImGui::Separator();
            ImGui::Text("Last Payload Preview");
            draw_payload_preview(last_payload);
        }

        ImGui::Separator();
        ImGui::Checkbox("Show ImGui demo window", &show_demo);
        ImGui::End();

        if (show_demo) {
            ImGui::ShowDemoWindow(&show_demo);
        }

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.09f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    channel.reset();
    shutdown_imgui(window);
    return 0;
}
