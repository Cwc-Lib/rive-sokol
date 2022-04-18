#include <rive/core/binary_reader.hpp>
#include <rive/file.hpp>
#include <rive/node.hpp>
#include <rive/shapes/clipping_shape.hpp>
#include <rive/shapes/rectangle.hpp>
#include <rive/shapes/shape.hpp>
#include "no_op_renderer.hpp"
#include "rive_file_reader.hpp"
#include <catch.hpp>
#include <cstdio>

TEST_CASE("draw rules load and sort correctly", "[draw rules]") {
    RiveFileReader reader("../../test/assets/draw_rule_cycle.riv");

    // auto file = reader.file();
    // auto node = file->artboard()->node("TopEllipse");
    // REQUIRE(node != nullptr);
    // REQUIRE(node->is<rive::Shape>());

    // auto shape = node->as<rive::Shape>();
    // REQUIRE(shape->clippingShapes().size() == 2);
    // REQUIRE(shape->clippingShapes()[0]->shape()->name() == "ClipRect2");
    // REQUIRE(shape->clippingShapes()[1]->shape()->name() == "BabyEllipse");

    // file->artboard()->updateComponents();

    // rive::NoOpRenderer renderer;
    // file->artboard()->draw(&renderer);
}