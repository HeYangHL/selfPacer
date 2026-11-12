#ifndef _SEND_PACKET_TO_NETWORK_HPP_
#define _SEND_PACKET_TO_NETWORK_HPP_

#include <memory>

#include "media_packet.hpp"

// 发送出口抽象：pacer 只负责“按节奏把包交出去”，具体发到哪由实现决定。
// 参考 webrtc 的 PacketSender 抽象（pacing_controller 只调 SendPacket）。
class PacketSender {
public:
    virtual ~PacketSender() = default;

    // 发送一个包（所有权转移）。初版可以先做成记录/UDP sendto。
    virtual void SendPacket(std::unique_ptr<MediaPacket> packet) = 0;

    // 一批处理完成后的可选回调，初版可留空。
    virtual void OnBatchComplete() {}
};

#endif
