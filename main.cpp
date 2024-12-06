#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <filesystem>
#include <iostream>
#include <json.hpp>
#include <unistd.h>
#include <fstream>
#include <thread>
#include <windows.h>
#include <string>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <fcntl.h>
struct processing_list_item {
    std::filesystem::path path;
    std::string status;
};
// thread safe processing list
std::mutex processing_list_mutex;
std::map<std::filesystem::path, std::string> processing_list;
class Process {
public:
    HANDLE hProcess = NULL;
    HANDLE hStdOut = NULL;
    HANDLE hStdErr = NULL;

    Process(HANDLE processHandle, HANDLE stdoutHandle, HANDLE stderrHandle)
            : hProcess(processHandle), hStdOut(stdoutHandle), hStdErr(stderrHandle) {}

    ~Process() {
        if (hStdOut != NULL) CloseHandle(hStdOut);
        if (hStdErr != NULL) CloseHandle(hStdErr);
        if (hProcess != NULL) CloseHandle(hProcess);
    }
};
void print_output(HANDLE hRead) {

    char buffer[256];
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        SDL_Log("%s", buffer);
    }

}

std::string read_output(HANDLE hRead) {
    std::ostringstream output;
    char buffer[256];
    DWORD bytesRead;
    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output << buffer;
    }
    return output.str();
}



Process popen_no_window(const std::string& command) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE stdoutRead, stdoutWrite, stderrRead, stderrWrite;

    // Create pipes for stdout
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) {
        throw std::runtime_error("Failed to create stdout pipe");
    }
    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        throw std::runtime_error("Failed to set stdout handle information");
    }

    // Create pipes for stderr
    if (!CreatePipe(&stderrRead, &stderrWrite, &sa, 0)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        throw std::runtime_error("Failed to create stderr pipe");
    }
    if (!SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        CloseHandle(stderrRead);
        CloseHandle(stderrWrite);
        throw std::runtime_error("Failed to set stderr handle information");
    }

    STARTUPINFO si = { sizeof(STARTUPINFO) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = NULL;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stderrWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::string cmd = "cmd.exe /C " + command; // Wrap command in cmd.exe
    if (!CreateProcess(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        CloseHandle(stderrRead);
        CloseHandle(stderrWrite);
        throw std::runtime_error("Failed to create process");
    }

    CloseHandle(stdoutWrite); // Close write ends in the parent
    CloseHandle(stderrWrite);

    return Process(pi.hProcess, stdoutRead, stderrRead);
}
bool extend_video(const std::filesystem::path& path) {
    {
        std::lock_guard<std::mutex> lock(processing_list_mutex);
        processing_list[path] = "Reading";
    }
// ffprobe that video
    auto cmd = "ffprobe.exe -i \"" + path.string() + "\" -show_format -show_streams -of json";

    auto ffprobe = popen_no_window(cmd.c_str());

    std::string probe = read_output(ffprobe.hStdOut);
    DWORD exit_status;
    GetExitCodeProcess(ffprobe.hProcess, &exit_status);
    if (exit_status != 0) {
        return false;
    }
    // check exit status
    SDL_Log("Output: %s", probe.c_str());
    auto j = nlohmann::json::parse(probe);
    // get first stream
    auto stream = j["streams"][0];
    auto ext = path.extension().string();
    auto video_duration = std::stod(stream["duration"].get<std::string>().c_str());
    auto duration = 181 - video_duration;
    if (duration < 0) {
        SDL_Log("Video is already 3 minutes or longer");
        return false;
    }
    if (!stream.contains("width") || !stream.contains("height") || !stream.contains("r_frame_rate")) {
        SDL_Log("Video is not supported");
        return false;
    }
    auto width = stream["width"].get<int>();
    auto height = stream["height"].get<int>();
    auto r_frame_rate = stream["r_frame_rate"].get<std::string>().c_str();
    SDL_Log("r_frame_rate: %s", r_frame_rate);
    // split r_frame_rate with '/'
    std::vector<std::string> parts;
    std::stringstream ss(r_frame_rate);
    std::string item;
    while (std::getline(ss, item, '/')) {
        parts.push_back(item);
    }
    auto frame_rate = std::stod(parts[0].c_str()) / std::stod(parts[1].c_str());
    auto time_base = stream["time_base"].get<std::string>();
    SDL_Log("time_base: %s", time_base.c_str());
    // hash input file path
    auto hash = std::hash<std::string>{}(path.string());
    // make a black screen video which matches the format of the input video, to fill 3 minutes.
    {
        std::lock_guard<std::mutex> lock(processing_list_mutex);
        processing_list[path] = "Creating black video";
    }
    auto black_video = ".\\temp\\" + std::to_string(hash) + "_black.mp4";
    if (std::filesystem::exists(black_video)) {
        std::filesystem::remove(black_video);
    }

    auto ffmpegCmd =
            "ffmpeg.exe -f lavfi -i color=c=black:s=" + std::to_string(width) + "x" + std::to_string(height) + ":r=" +
            std::to_string(frame_rate) + ":d=" + std::to_string(duration) + " -vcodec " +
            stream["codec_name"].get<std::string>() + " -pix_fmt " + stream["pix_fmt"].get<std::string>() + " -b:v " +
            stream["bit_rate"].get<std::string>() + " -time_base " + time_base + " -y \"" + black_video + "\" 2>&1";
    SDL_Log("ffmpeg command: %s", ffmpegCmd.c_str());
    auto ffmpeg = popen_no_window(ffmpegCmd.c_str());

    print_output(ffmpeg.hStdOut);
    DWORD ffmpeg_exit_status;
    GetExitCodeProcess(ffmpeg.hProcess, &ffmpeg_exit_status);
    if (ffmpeg_exit_status != 0) {
        return false;
    }


    {
        std::lock_guard<std::mutex> lock(processing_list_mutex);
        processing_list[path] = "Concatenating";
    }

    auto concat_list = ".\\temp\\" + std::to_string(hash) + "_concat_list.txt";
    // write concat list
    std::ofstream concat_list_file(concat_list);
    concat_list_file << "file '" << path.string() << "'\n";
    concat_list_file << "file '" << std::to_string(hash) + "_black.mp4" << "'\n";
    concat_list_file.close();
    auto output = path.parent_path().string() + "\\" + path.stem().string() + "_extended.mp4";
    // remove if exists
    if (std::filesystem::exists(output)) {
        std::filesystem::remove(output);
    }
    auto concatCmd = "ffmpeg.exe -safe 0 -f concat -i \"" + concat_list + "\" -c copy \"" + output + "\" 2>&1";
    SDL_Log("concat command: %s", concatCmd.c_str());
    auto concat = popen_no_window(concatCmd.c_str());

    print_output(concat.hStdOut);
    DWORD concat_exit_status;
    GetExitCodeProcess(concat.hProcess, &concat_exit_status);
    if (concat_exit_status != 0) {
        return false;
    }



    return true;

}

int main(int argc, char *argv[]) {
    // mkdir temp
    std::filesystem::create_directory(".\\temp\\");
    // set PATH env
    std::string path = (std::filesystem::current_path() / "bin").string();
    SDL_Log("PATH: %s", path.c_str());
    _putenv_s("PATH", path.c_str());
    SDL_Init(SDL_INIT_EVERYTHING);
    SDL_Log("Hello World!");
    SDL_Window *window = SDL_CreateWindow("deshortify", 100, 100, 640, 480, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    int screenWidth, screenHeight;
    SDL_GetWindowSize(window, &screenWidth, &screenHeight);
    bool quit = false;
    // Load font
    TTF_Init();
    TTF_Font *font = TTF_OpenFont("assets/fonts/notosanscjkjp.ttf", 24);
    SDL_Color textColor = {255, 255, 255, 255};
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
                break;
            }
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_RESIZED) {
                screenWidth = e.window.data1;
                screenHeight = e.window.data2;
                SDL_RenderSetLogicalSize(renderer, screenWidth, screenHeight);
                SDL_RenderSetViewport(renderer, NULL);
                SDL_RenderSetScale(renderer, 1.0f, 1.0f);


                break;
            }
            if (e.type == SDL_DROPFILE) {
                std::filesystem::path path(e.drop.file);
                SDL_Log("Dropped file: %ls", path.c_str());
                {
                    std::lock_guard<std::mutex> lock(processing_list_mutex);
                    processing_list[path] = "Processing";
                }
                std::thread t([](const std::filesystem::path& path) {
                    if (extend_video(path)) {
                        std::lock_guard<std::mutex> lock(processing_list_mutex);
                        processing_list[path] = "Done";
                    } else {
                        std::lock_guard<std::mutex> lock(processing_list_mutex);
                        processing_list[path] = "Error";
                    }
                }, path);
                t.detach();

            }
        }
        // Render
        SDL_RenderClear(renderer);

        // drag and drop here text
        processing_list_mutex.lock();
        size_t processing_list_size = processing_list.size();
        processing_list_mutex.unlock();
        if (processing_list_size > 0) {
            std::lock_guard<std::mutex> lock(processing_list_mutex);
            std::stringstream ss;

            for (auto& [path, status] : processing_list) {
                ss << path.filename().string() << ": " << status << std::endl;
            }
            SDL_Surface *textSurface = TTF_RenderText_Blended_Wrapped(font, ss.str().c_str(), textColor, screenWidth);
            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, textSurface);

            SDL_Rect srcRect = {0, 0, textSurface->w, textSurface->h};
            SDL_Rect dstRect = {screenWidth/2 - textSurface->w / 2, screenHeight/2 - textSurface->h / 2, textSurface->w, textSurface->h};
            SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);
            SDL_FreeSurface(textSurface);
            SDL_DestroyTexture(texture);
        } else {
            SDL_Surface *textSurface = TTF_RenderText_Solid(font, "Drag and drop here", textColor);
            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, textSurface);

            SDL_Rect srcRect = {0, 0, textSurface->w, textSurface->h};
            SDL_Rect dstRect = {screenWidth / 2 - textSurface->w / 2, screenHeight / 2 - textSurface->h / 2,
                                textSurface->w, textSurface->h};
            SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);
            SDL_FreeSurface(textSurface);
            SDL_DestroyTexture(texture);
        }
        if(quit){
            SDL_SetWindowTitle(window, "Cleaning up");
            std::filesystem::remove_all(".\\temp\\");
        }
        SDL_RenderPresent(renderer);
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
