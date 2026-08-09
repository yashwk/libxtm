#include <gtest/gtest.h>
#include "xtm/partition/Quadtree.hpp"
#include "xtm/analyzer/Selector.hpp"
#include "xtm/predictor/Predictors.hpp"

using namespace xtm::partition;
using namespace xtm::analyzer;
using namespace xtm::terrain;
using namespace xtm::predictor;

class MockPredictor : public Predictor {
public:
    PredictorId id() const override { return PredictorId::Left; }
    const char* name() const override { return "Mock"; }
    void encode(const BlockView& block, PredictionResult& res) const override {
        res.residuals.resize(block.width * block.height);
        if (block.width == 64) {
            // Force high entropy for 64x64 so it splits
            for (uint32_t i = 0; i < block.width * block.height; ++i) {
                res.residuals[i] = (i % 256) - 128; // high enough variance to cost > 1 bit/px
            }
        } else {
            // Force 0 entropy for 32x32 so it prefers the split
            for (uint32_t i = 0; i < block.width * block.height; ++i) {
                res.residuals[i] = 0;
            }
        }
    }
    void decode(const PredictionResult& /*res*/, MutableBlockView& /*block*/) const override {}
};

TEST(QuadtreeTest, ZOrderTraversal) {
    IntGrid grid;
    grid.width = 64;
    grid.height = 64;
    grid.data.resize(64 * 64, 0);

    MockPredictor mock_pred;
    std::vector<const Predictor*> preds = { &mock_pred };
    // Context model Simple to evaluate entropy
    PredictorSelector selector(preds, PipelineType::Predictor, 1, xtm::coding::ContextModel::Simple);
    
    double total_bits = 0;
    std::vector<QuadtreeNode> nodes;
    QuadtreePartitioner::partition(grid, 64, 32, selector, total_bits, nodes);
    
    // 64x64 splitting down to 32x32 should yield 4 blocks.
    ASSERT_EQ(nodes.size(), 4);
    
    // Z-order: Top-Left, Top-Right, Bottom-Left, Bottom-Right
    EXPECT_EQ(nodes[0].block.x_offset, 0);
    EXPECT_EQ(nodes[0].block.y_offset, 0);
    
    EXPECT_EQ(nodes[1].block.x_offset, 32);
    EXPECT_EQ(nodes[1].block.y_offset, 0);
    
    EXPECT_EQ(nodes[2].block.x_offset, 0);
    EXPECT_EQ(nodes[2].block.y_offset, 32);
    
    EXPECT_EQ(nodes[3].block.x_offset, 32);
    EXPECT_EQ(nodes[3].block.y_offset, 32);
}
