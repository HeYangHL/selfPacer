#ifndef _MEDIA_PACKET_HPP_
#define _MEDIA_PACKET_HPP_

#include <iostream>
#include <vector>

class MediaPacket{
public:
    MediaPacket() = default;
    MediaPacket(std::vector<uint8_t> data, uint32_t flow_id,
            int priority, bool is_key_frame = false): data_(std::move(data)), payload_size_(data_.size()),
            flow_id_(flow_id), priority_(priority),
            is_key_frame_(is_key_frame) {};
    size_t size() const { return payload_size_; }     // 债务用
    uint32_t flow_id() const { return flow_id_; }     // 队列分流用
    int priority() const { return priority_; }        // 队列排序用
    bool is_key_frame() const { return is_key_frame_; }
    const std::vector<uint8_t>& data() const { return data_; }  // 发送用
private:
    std::vector<uint8_t> data_;//任意格式网络数据
    size_t payload_size_;//计入债务的大小
    uint32_t flow_id_;//流标识，替代webrtc的ssrc
    int priority_;//0：音频 1：音频重传 2：视频重传 3：媒体 4：填充
    int64_t enqueue_time_;//入队时刻(队列用它算oldest)
    bool is_key_frame_ = false;//关键帧优先
};


#endif