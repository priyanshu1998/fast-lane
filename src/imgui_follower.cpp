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
#include <optional>
#include <string>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

void glfw_error_callback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

bool init_imgui(GLFWwindow*& window, const char* title)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    window = glfwCreateWindow(960, 640, title, nullptr, nullptr);
    if (window == nullptr)
    {
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

void draw_snapshot_table(const std::vector<std::uint8_t>& bytes)
{
    const auto values = fast_lane::decode_u64_sequence(bytes);
    if (ImGui::BeginTable("snapshot-values",
                          3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 220)))
    {
        ImGui::TableSetupColumn("Index");
        ImGui::TableSetupColumn("uint64");
        ImGui::TableSetupColumn("Hex");
        ImGui::TableHeadersRow();

        for (std::size_t row = 0; row < values.size(); ++row)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", row);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(values[row]));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("0x%016llx", static_cast<unsigned long long>(values[row]));
        }
        ImGui::EndTable();
    }
}

} // namespace

int main()
{
    GLFWwindow* window = nullptr;
    if (!init_imgui(window, "Fast Lane Follower"))
    {
        return 1;
    }

    std::array<char, 128> channel_name{};
    std::snprintf(channel_name.data(), channel_name.size(), "%s", "fast_lane_demo");

    std::optional<fast_lane::SharedMemoryChannel> channel;
    std::optional<fast_lane::CopyOnWriteBuffer> local_buffer;
    std::optional<fast_lane::CopyOnWriteBuffer> branch_buffer;
    fast_lane::SharedSnapshot snapshot;

    std::uint64_t last_seen = 0;
    std::uint64_t updates = 0;
    int poll_interval_ms = 50;
    int mutate_offset = 0;
    int mutate_value = 255;
    bool auto_poll = true;
    bool show_demo = false;
    std::string status = "Connect to a leader channel to begin reading.";
    auto next_poll = Clock::now();

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
        ImGui::Begin("Fast Lane Follower");

        ImGui::InputText("Channel", channel_name.data(), channel_name.size());
        ImGui::InputInt("Poll interval ms", &poll_interval_ms);
        poll_interval_ms = std::max(poll_interval_ms, 10);

        if (!channel.has_value())
        {
            if (ImGui::Button("Connect Follower"))
            {
                try
                {
                    channel.emplace(
                        fast_lane::SharedMemoryChannel::open_follower(channel_name.data()));
                    last_seen = 0;
                    updates = 0;
                    status = "Follower connected.";
                }
                catch (const std::exception& error)
                {
                    status = std::string("Connect failed: ") + error.what();
                }
            }
        }
        else
        {
            if (ImGui::Button("Disconnect"))
            {
                channel.reset();
                local_buffer.reset();
                branch_buffer.reset();
                status = "Follower disconnected.";
            }
            ImGui::SameLine();
            if (ImGui::Button("Poll Once"))
            {
                try
                {
                    if (channel->try_read(last_seen, snapshot))
                    {
                        last_seen = snapshot.sequence;
                        ++updates;
                        local_buffer.emplace(snapshot.bytes);
                        branch_buffer = local_buffer->fork();
                        status = "Read a new snapshot.";
                    }
                    else
                    {
                        status = "No newer stable snapshot.";
                    }
                }
                catch (const std::exception& error)
                {
                    status = std::string("Poll failed: ") + error.what();
                }
            }
            ImGui::SameLine();
            ImGui::Checkbox("Auto Poll", &auto_poll);
        }

        if (channel.has_value() && auto_poll && Clock::now() >= next_poll)
        {
            try
            {
                if (channel->try_read(last_seen, snapshot))
                {
                    last_seen = snapshot.sequence;
                    ++updates;
                    local_buffer.emplace(snapshot.bytes);
                    branch_buffer = local_buffer->fork();
                    status = "Auto-read a new snapshot.";
                }
            }
            catch (const std::exception& error)
            {
                status = std::string("Auto poll failed: ") + error.what();
                auto_poll = false;
            }
            next_poll = Clock::now() + std::chrono::milliseconds(poll_interval_ms);
        }

        ImGui::Separator();
        ImGui::Text("State: %s", channel.has_value() ? "follower connected" : "not connected");
        ImGui::Text("Last sequence: %llu", static_cast<unsigned long long>(last_seen));
        ImGui::Text("Snapshots read: %llu", static_cast<unsigned long long>(updates));
        ImGui::Text("Snapshot bytes: %zu", snapshot.bytes.size());
        ImGui::TextWrapped("Status: %s", status.c_str());

        if (!snapshot.bytes.empty())
        {
            ImGui::Separator();
            ImGui::Text("Snapshot Values");
            draw_snapshot_table(snapshot.bytes);
        }

        if (local_buffer.has_value() && branch_buffer.has_value() && !local_buffer->empty())
        {
            ImGui::Separator();
            ImGui::Text("Copy-On-Write Local Mutation");
            mutate_offset =
                std::clamp(mutate_offset, 0, static_cast<int>(branch_buffer->size() - 1));
            mutate_value = std::clamp(mutate_value, 0, 255);
            ImGui::InputInt("Byte offset", &mutate_offset);
            ImGui::InputInt("New byte value", &mutate_value);
            mutate_offset =
                std::clamp(mutate_offset, 0, static_cast<int>(branch_buffer->size() - 1));
            mutate_value = std::clamp(mutate_value, 0, 255);
            if (ImGui::Button("Mutate Branch Copy"))
            {
                branch_buffer->set(static_cast<std::size_t>(mutate_offset),
                                   static_cast<std::uint8_t>(mutate_value));
                status =
                    "Mutated branch copy. Shared snapshot and local source view are unchanged.";
            }

            const auto offset = static_cast<std::size_t>(mutate_offset);
            ImGui::Text("Local source byte[%d]: %d",
                        mutate_offset,
                        static_cast<int>(local_buffer->at(offset)));
            ImGui::Text("Branch copy byte[%d]: %d",
                        mutate_offset,
                        static_cast<int>(branch_buffer->at(offset)));
        }

        ImGui::Separator();
        ImGui::Checkbox("Show ImGui demo window", &show_demo);
        ImGui::End();

        if (show_demo)
        {
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
