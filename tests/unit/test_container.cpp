#include <gtest/gtest.h>
#include "xtm/container/IO.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <random>

using namespace xtm::container;

namespace {

class ContainerTest : public ::testing::Test {
protected:
    std::string temp_dir_;

    void SetUp() override {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        temp_dir_ = std::filesystem::temp_directory_path() / ("xtm_test_container_" + std::to_string(dis(gen)));
        std::filesystem::create_directory(temp_dir_);
    }

    void TearDown() override {
        if (std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    }

    std::string get_temp_file(const std::string& name) {
        return (std::filesystem::path(temp_dir_) / name).string();
    }
};

} // namespace

TEST_F(ContainerTest, RoundtripWriterReader) {
    std::string test_file = get_temp_file("test_output.xtm");
    
    XtmHeader header;
    header.transform.origin_x = 100.0;
    header.transform.origin_y = 400.0;
    header.wkt_projection = "PROJCS[\"WGS 84 / Pseudo-Mercator\"]";
    header.grid_width = 1024;
    header.grid_height = 1024;
    header.precision = 0.5;
    header.context_model = 1;
    
    {
        XtmWriter writer(test_file, header);
        
        std::vector<uint8_t> block1 = {0xAA, 0xBB, 0xCC};
        std::vector<uint8_t> block2 = {0x11, 0x22, 0x33, 0x44};
        
        XtmWriter::PendingBlock pb1;
        pb1.x = 0; pb1.y = 0; pb1.width = 512; pb1.height = 512; pb1.bitstream = block1;
        
        XtmWriter::PendingBlock pb2;
        pb2.x = 512; pb2.y = 0; pb2.width = 512; pb2.height = 512; pb2.bitstream = block2;

        writer.write_superblock(0, {pb1, pb2});
        
        writer.finalize();
    }
    
    {
        XtmReader reader(test_file);
        
        const auto& h = reader.get_header();
        EXPECT_EQ(h.wkt_projection, "PROJCS[\"WGS 84 / Pseudo-Mercator\"]");
        EXPECT_EQ(h.grid_width, 1024u);
        EXPECT_EQ(h.precision, 0.5);
        EXPECT_EQ(h.context_model, 1u);
        
        const auto& index = reader.get_index();
        ASSERT_EQ(index.size(), 2u);
        
        EXPECT_EQ(index[0].block_width, 512u);
        EXPECT_EQ(index[0].byte_length, 3u);
        
        EXPECT_EQ(index[1].block_width, 512u);
        EXPECT_EQ(index[1].byte_length, 4u);
        
        auto b1 = reader.read_block(index[0]);
        auto b2 = reader.read_block(index[1]);
        
        EXPECT_EQ(b1.size(), 3u);
        EXPECT_EQ(b1[0], 0xAA);
        
        EXPECT_EQ(b2.size(), 4u);
        EXPECT_EQ(b2[0], 0x11);
    }
}

TEST_F(ContainerTest, RejectsBadMagic) {
    std::string test_file = get_temp_file("test_bad_magic.xtm");
    {
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        os.write("NOPE", 4);
        os.write("\0\0\0\0", 4);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
}

TEST_F(ContainerTest, RejectsUnsupportedVersion) {
    std::string test_file = get_temp_file("test_bad_version.xtm");
    {
        XtmHeader header;
        header.version = 99;
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        header.write(os);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
}

TEST_F(ContainerTest, RejectsTruncatedHeader) {
    std::string test_file = get_temp_file("test_truncated.xtm");
    {
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        os.write("XTM\0", 4); // Header cut off after magic
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
}

TEST_F(ContainerTest, RejectsIndexOffsetBeyondFile) {
    std::string test_file = get_temp_file("test_bad_index.xtm");
    {
        XtmHeader header;
        header.index_offset = 1ull << 40; // Far beyond any realistic file
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        header.write(os);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
}

TEST_F(ContainerTest, RejectsBlockRegionBeyondFile) {
    std::string test_file = get_temp_file("test_bad_block.xtm");
    {
        XtmWriter writer(test_file, XtmHeader{});
        XtmWriter::PendingBlock pb;
        pb.x = 0; pb.y = 0; pb.width = 64; pb.height = 64; pb.bitstream = {0xDE, 0xAD, 0xBE, 0xEF};
        writer.write_superblock(0, {pb});
        writer.finalize();
    }
    {
        XtmReader reader(test_file);
        BlockIndexEntry bad_entry;
        bad_entry.block_x = 0;
        bad_entry.block_y = 0;
        bad_entry.block_width = 64;
        bad_entry.block_height = 64;
        bad_entry.byte_offset = 1ull << 40;
        bad_entry.byte_length = 4;
        EXPECT_THROW(reader.read_block(bad_entry), std::runtime_error);
    }
}

TEST_F(ContainerTest, RejectsInvalidContextModel) {
    std::string test_file = get_temp_file("test_bad_context.xtm");
    {
        XtmHeader header;
        header.context_model = 42;
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        header.write(os);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
}

TEST_F(ContainerTest, RejectsZeroLengthBlockRegionOutsideFile) {
    std::string test_file = get_temp_file("test_bad_block2.xtm");
    {
        XtmWriter writer(test_file, XtmHeader{});
        XtmWriter::PendingBlock pb;
        pb.x = 0; pb.y = 0; pb.width = 64; pb.height = 64; pb.bitstream = {0xAA};
        writer.write_superblock(0, {pb});
        writer.finalize();
    }
    {
        XtmReader reader(test_file);
        BlockIndexEntry bad_entry;
        bad_entry.byte_offset = 1ull << 40;
        bad_entry.byte_length = 0;
        EXPECT_THROW(reader.read_block(bad_entry), std::runtime_error);
    }
}

TEST_F(ContainerTest, OutputIsDeterministicAcrossWriteOrder) {
    const std::string file_a = get_temp_file("test_det_a.xtm");
    const std::string file_b = get_temp_file("test_det_b.xtm");

    std::vector<uint8_t> block1 = {0xAA, 0xBB, 0xCC};
    std::vector<uint8_t> block2 = {0x11, 0x22, 0x33, 0x44};
    std::vector<uint8_t> block3 = {0xDE, 0xAD, 0xBE, 0xEF};

    XtmWriter::PendingBlock pb1, pb2, pb3;
    pb1.x = 0; pb1.y = 0; pb1.width = 64; pb1.height = 64; pb1.bitstream = block1; pb1.sequence_id = 1;
    pb2.x = 0; pb2.y = 64; pb2.width = 64; pb2.height = 64; pb2.bitstream = block2; pb2.sequence_id = 2;
    pb3.x = 64; pb3.y = 0; pb3.width = 64; pb3.height = 64; pb3.bitstream = block3; pb3.sequence_id = 3;

    {
        XtmWriter writer(file_a, XtmHeader{});
        writer.write_superblock(0, {pb1});
        writer.write_superblock(1, {pb2});
        writer.write_superblock(2, {pb3});
        writer.finalize();
    }
    {
        XtmWriter writer(file_b, XtmHeader{});
        writer.write_superblock(2, {pb3});
        writer.write_superblock(1, {pb2});
        writer.write_superblock(0, {pb1});
        writer.finalize();
    }

    std::ifstream ia(file_a, std::ios::binary);
    std::ifstream ib(file_b, std::ios::binary);
    std::vector<uint8_t> bytes_a((std::istreambuf_iterator<char>(ia)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> bytes_b((std::istreambuf_iterator<char>(ib)), std::istreambuf_iterator<char>());
    EXPECT_EQ(bytes_a, bytes_b);
}
