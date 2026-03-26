#ifndef VIDEO_CREATOR_API_H
#define VIDEO_CREATOR_API_H

#include <string>

/**
 * @file VideoCreatorAPI.h
 * @brief 对外暴露的视频渲染 API。
 */

namespace VideoCreator
{
    /**
     * @brief 从 JSON 文件渲染视频。
     * @param config_path 配置文件路径。
     * @param error 可选错误输出参数。
     * @return 渲染成功返回 true，否则返回 false。
     */
    bool RenderFromJson(const std::string &config_path, std::string *error = nullptr);

    /**
     * @brief 从 JSON 字符串渲染视频。
     * @param json_string JSON 内容字符串。
     * @param error 可选错误输出参数。
     * @return 渲染成功返回 true，否则返回 false。
     */
    bool RenderFromJsonString(const std::string &json_string, std::string *error = nullptr);
}

#endif // VIDEO_CREATOR_API_H
