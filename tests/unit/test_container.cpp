#include <gtest/gtest.h>
#include "xtm/container/IO.hpp"
#include <filesystem>

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
