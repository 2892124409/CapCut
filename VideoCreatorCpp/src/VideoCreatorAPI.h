#ifndef VIDEO_CREATOR_API_H
#define VIDEO_CREATOR_API_H

#include <string>

namespace VideoCreator
{
    // 从 JSON 文件路径渲染视频，返回成功/失败，错误信息写入 error（可选）
    bool RenderFromJson(const std::string &config_path, std::string *error = nullptr);

    // 从 JSON 字符串渲染视频，返回成功/失败，错误信息写入 error（可选）
    bool RenderFromJsonString(const std::string &json_string, std::string *error = nullptr);
}

#endif // VIDEO_CREATOR_API_H
