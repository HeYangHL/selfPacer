#include "media_packet.hpp"

int MediaPacket::set_playload(const uint8_t* data, size_t size)
{
    if (size == 0) {
        data_.clear();
        payload_size_ = 0;
        return 0;
    }
    if (data == nullptr) {
        return -1;
    }
    // 拷贝入参，避免包在队列里时引用到已释放的调用方缓冲区
    data_.assign(data, data + size);
    payload_size_ = data_.size();
    return 0;
}
int MediaPacket::set_is_key_frame(bool Ikey)
{
    is_key_frame_ = Ikey;
    return 0;
}
int MediaPacket::set_flow_id(uint32_t flowId)
{
    flow_id_ = flowId;
    return 0;
}
int MediaPacket::set_size(size_t payloadSize)
{
    payload_size_ = payloadSize;
    return 0;
}