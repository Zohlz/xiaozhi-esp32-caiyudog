#ifndef OGG_DEMUXER_H_
#define OGG_DEMUXER_H_

#include <functional>
#include <cstdint>
#include <cstring>
#include <vector>

class OggDemuxer {
private:
    enum ParseState : int8_t {
        FIND_PAGE,
        PARSE_HEADER,
        PARSE_SEGMENTS,
        PARSE_DATA
    };

    struct Opus_t {
        bool    head_seen{false};
        bool    tags_seen{false};
        int     sample_rate{48000};
    };


    // 使用固定大小的缓冲区避免动态分配
    //
    // packet_buf 由 8KB 降到 4KB：整个 context_t 是堆上一次分配、长期复用的一块内存，
    // 而 C3 没有 PSRAM，它的大小直接决定「堆里最大连续空闲块」还剩多少 —— 对话走 https
    // 时 esp-tls 每读一条 TLS 记录都要现分配一块记录长度的接收缓冲（服务器一条记录
    // 16KB），省下这 4KB 就是给它腾地方。
    // 4KB 足够放 Opus 音频包：20ms 帧、128kbps 约 320 字节，60ms 帧、320kbps 也才 2.4KB。
    // 只有内嵌大封面图的 OpusTags 会超（8KB 也未必够），超了会打 "包缓冲区溢出" 并导致
    // 整条流被丢弃；真遇到这种文件再把这个值调回去。
    struct context_t {
        bool packet_continued{false};   // 当前包是否跨多个段
        uint8_t header[27];             // Ogg页头
        uint8_t seg_table[255];         // 当前存储的段表
        uint8_t packet_buf[4096];       // 4KB包缓冲区
        size_t packet_len = 0;          // 缓冲区中累计的数据长度
        size_t seg_count = 0;           // 当前页段数
        size_t seg_index = 0;           // 当前处理的段索引
        size_t data_offset = 0;         // 解析当前阶段已读取的字节数
        size_t bytes_needed = 0;        // 解析当前字段还需要读取的字节数
        size_t seg_remaining = 0;       // 当前段剩余需要读取的字节数
        size_t body_size = 0;           // 数据体总大小
        size_t body_offset = 0;         // 数据体已读取的字节数
    };
    
public:
    OggDemuxer() {
        Reset();
    }
    
    void Reset();
    
    size_t Process(const uint8_t* data, size_t size);

    /// @brief 设置解封装完毕后回调处理函数
    /// @param on_demuxer_finished 
    void OnDemuxerFinished(std::function<void(const uint8_t* data, int sample_rate, size_t len)> on_demuxer_finished) {
        on_demuxer_finished_ = on_demuxer_finished;
    }
private:

    ParseState  state_ = ParseState::FIND_PAGE;
    context_t   ctx_;
    Opus_t      opus_info_;
    std::function<void(const uint8_t*, int, size_t)> on_demuxer_finished_;
};

#endif