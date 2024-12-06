#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <filesystem>
#include <iostream>
#include <json.hpp>
#include <unistd.h>
#include <fstream>
#include <thread>
struct processing_list_item {
    std::filesystem::path path;
    std::string status;
};
// thread safe processing list
std::mutex processing_list_mutex;
std::map<std::filesystem::path, std::string> processing_list;

void popen_print(FILE* stream) {
    char buffer[256];
    while (!feof(stream)) {
        if (fgets(buffer, 256, stream) != NULL) {
            SDL_Log("ffmpeg output: %s", buffer);
        }
    }

}
bool extend_video(const std::filesystem::path& path) {
    {
        std::lock_guard<std::mutex> lock(processing_list_mutex);
        processing_list[path] = "Reading";
    }
// ffprobe that video
    auto cmd  = "ffprobe.exe -i \"" + path.string() + "\" -show_format -show_streams -of json";

    FILE* stream;
    stream = popen(cmd.c_str(), "r");
    if (stream) {
        std::string probe;
        char buffer[256];
        while (!feof(stream)) {
            if (fgets(buffer, 256, stream) != NULL) {
                SDL_Log("ffmpeg probe: %s", buffer);
                probe += buffer;
            }
        }
        int exit_status = pclose(stream);
        if (exit_status != 0) {
            SDL_Log("Error: %s", strerror(exit_status));
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
        if(duration < 0) {
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
        // split r_frame_rate with '/'
        std::vector<std::string> parts;
        std::stringstream ss(r_frame_rate);
        std::string item;
        while (std::getline(ss, item, '/')) {
            parts.push_back(item);
        }
        auto frame_rate = std::stod(parts[0].c_str()) / std::stod(parts[1].c_str());
        auto time_base = stream["time_base"].get<std::string>().c_str();
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

        auto ffmpegCmd = "ffmpeg.exe -f lavfi -i color=c=black:s=" + std::to_string(width) + "x" + std::to_string(height) + ":r=" + std::to_string(frame_rate) + ":d=" + std::to_string(duration) + " -vcodec " + stream["codec_name"].get<std::string>() + " -pix_fmt " + stream["pix_fmt"].get<std::string>() + " -b:v " + stream["bit_rate"].get<std::string>() + " -time_base " + time_base + " -y \"" + black_video + "\" 2>&1";
        SDL_Log("ffmpeg command: %s", ffmpegCmd.c_str());
        auto ffmpeg = popen(ffmpegCmd.c_str(), "r");
        if (ffmpeg) {
            popen_print(ffmpeg);
            int exit_status = pclose(ffmpeg);
            if (exit_status != 0) {
                SDL_Log("Error: %s", strerror(exit_status));
                return false;
            }
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
        auto concat = popen(concatCmd.c_str(), "r");
        if (concat) {
            popen_print(concat);
            int exit_status = pclose(concat);
            if (exit_status != 0) {
                SDL_Log("Error: %s", strerror(exit_status));
                return false;
            }
        }
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
