#include <gtest/gtest.h>
#include "xtm/container/IO.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace xtm::container;

TEST(ContainerTest, RoundtripWriterReader) {
    std::string test_file = "test_output.xtm";
    
    XtmHeader header;
    header.min_x = 100.0;
    header.min_y = 200.0;
    header.max_x = 300.0;
    header.max_y = 400.0;
    header.epsg_crs = 4326;
    header.grid_width = 1024;
    header.grid_height = 1024;
    header.context_model = 1;
    
    {
        XtmWriter writer(test_file, header);
        
        std::vector<uint8_t> block1 = {0xAA, 0xBB, 0xCC};
        std::vector<uint8_t> block2 = {0x11, 0x22, 0x33, 0x44};
        
        writer.write_block(0, 0, 512, 512, block1);
        writer.write_block(512, 0, 512, 512, block2);
        
        writer.finalize();
    }
    
    {
        XtmReader reader(test_file);
        
        const auto& h = reader.get_header();
        EXPECT_EQ(h.epsg_crs, 4326u);
        EXPECT_EQ(h.grid_width, 1024u);
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
    
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsBadMagic) {
    std::string test_file = "test_bad_magic.xtm";
    {
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        os.write("NOPE", 4);
        os.write("\0\0\0\0", 4);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsUnsupportedVersion) {
    std::string test_file = "test_bad_version.xtm";
    {
        XtmHeader header;
        header.version = 99;
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        header.write(os);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsTruncatedHeader) {
    std::string test_file = "test_truncated.xtm";
    {
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        os.write("XTM\0", 4); // Header cut off after magic
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsIndexOffsetBeyondFile) {
    std::string test_file = "test_bad_index.xtm";
    {
        XtmHeader header;
        header.index_offset = 1ull << 40; // Far beyond any realistic file
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        header.write(os);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsBlockRegionBeyondFile) {
    std::string test_file = "test_bad_block.xtm";
    {
        XtmWriter writer(test_file, XtmHeader{});
        writer.write_block(0, 0, 64, 64, {0xDE, 0xAD, 0xBE, 0xEF});
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
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsInvalidContextModel) {
    std::string test_file = "test_bad_context.xtm";
    {
        XtmHeader header;
        header.context_model = 42;
        std::ofstream os(test_file, std::ios::binary | std::ios::trunc);
        header.write(os);
    }
    EXPECT_THROW(XtmReader reader(test_file), std::runtime_error);
    std::filesystem::remove(test_file);
}

TEST(ContainerTest, RejectsZeroLengthBlockRegionOutsideFile) {
    std::string test_file = "test_bad_block2.xtm";
    {
        XtmWriter writer(test_file, XtmHeader{});
        writer.write_block(0, 0, 64, 64, {0xAA});
        writer.finalize();
    }
    {
        XtmReader reader(test_file);
        BlockIndexEntry bad_entry;
        bad_entry.byte_offset = 1ull << 40;
        bad_entry.byte_length = 0;
        EXPECT_THROW(reader.read_block(bad_entry), std::runtime_error);
    }
    std::filesystem::remove(test_file);
}
