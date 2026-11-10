#ifndef _MEDIA_PACKET_HPP_
#define _MEDIA_PACKET_HPP_

#include <iostream>
#include <vector>
#include <cstdint>

static constexpr size_t kNumMediaTypes = 6;
enum class RtpPacketMediaType : size_t {
  kAudio,                         // Audio media packets.
  kVideo,                         // Video media packets.
  NetWork,                        //NetWork packets
  kRetransmission,                // Retransmisions, sent as response to NACK.
  kForwardErrorCorrection,        // FEC packets.
  kPadding = kNumMediaTypes - 1,  // RTX or plain padding sent to maintain BWE.
};

// 包类型 -> 优先级 level（越小越优先）。
// 当前按枚举声明顺序依次映射（恒等映射），后续要细化时只改这里。
inline int GetPriorityForType(RtpPacketMediaType t)
{
    switch (t) {
        case RtpPacketMediaType::kAudio:                  return 0;
        case RtpPacketMediaType::kVideo:                  return 1;
        case RtpPacketMediaType::NetWork:                 return 2;
        case RtpPacketMediaType::kRetransmission:         return 3;
        case RtpPacketMediaType::kForwardErrorCorrection: return 4;
        case RtpPacketMediaType::kPadding:                return 5;
    }
    return 5;
}

class MediaPacket{
public:
    MediaPacket() = default;
    size_t get_size() const { return payload_size_; }     // 债务用
    int set_size(size_t payloadSize);
    uint32_t get_flow_id() const { return flow_id_; }     // 队列分流用
    int set_flow_id(uint32_t flowId);
    void set_type(RtpPacketMediaType type) { type_ = type; }   // 包类型
    RtpPacketMediaType get_type() const { return type_; }
    // 优先级：由包类型经 GetPriorityForType 映射得到
    int get_priority() const { return GetPriorityForType(type_); }
    bool get_is_key_frame() const { return is_key_frame_; }
    int set_is_key_frame(bool Ikey);
    const std::vector<uint8_t>& get_data() const { return data_; }  // 发送用
    int set_playload(const uint8_t* data, size_t size);
    int set_playload(const std::vector<uint8_t>& data)
    {
        return set_playload(data.data(), data.size());
    }
private:
    std::vector<uint8_t> data_;//任意格式网络数据（拥有所有权，拷贝自入参）
    size_t payload_size_ = 0;//计入债务的大小
    uint32_t flow_id_ = 0;//流标识，替代webrtc的ssrc
    // 包类型；当前其枚举值直接作为优先级 level（恒等映射，枚举顺序即优先级顺序）
    RtpPacketMediaType type_ = RtpPacketMediaType::NetWork;
    int64_t enqueue_time_ = 0;//入队时刻(队列用它算oldest)
    bool is_key_frame_ = false;//关键帧优先
    // uint64_t stream_id;     //支持多流同时入队，每个流都有独立的id，独立的queue。
};


#endif