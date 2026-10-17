#include <doctest/doctest.h>

#include "ui/animation.hpp"

using playfin::ui::Animator;
using playfin::ui::Easing;
using playfin::ui::ease;

TEST_CASE("easing curves") {
    CHECK(ease(Easing::Linear, 0.5f) == doctest::Approx(0.5f));
    CHECK(ease(Easing::EaseOutCubic, 0.5f) == doctest::Approx(0.875f));
    CHECK(ease(Easing::EaseInOutCubic, 0.25f) == doctest::Approx(0.0625f));
    CHECK(ease(Easing::EaseInOutCubic, 0.75f) == doctest::Approx(0.9375f));
    CHECK(ease(Easing::Linear, -1.0f) == doctest::Approx(0.0f));
    CHECK(ease(Easing::Linear, 2.0f) == doctest::Approx(1.0f));
}

TEST_CASE("a tween moves its target and finishes exactly") {
    Animator animator;
    float value = 0;
    animator.animate(&value, 100, 100, Easing::Linear, nullptr);
    animator.tick(50);
    CHECK(value == doctest::Approx(50));
    animator.tick(60);
    CHECK(value == doctest::Approx(100));
    CHECK_FALSE(animator.active());
}

TEST_CASE("animating the same target again starts from where it is") {
    Animator animator;
    float value = 0;
    animator.animate(&value, 100, 100, Easing::Linear, nullptr);
    animator.tick(50);
    animator.animate(&value, 0, 100, Easing::Linear, nullptr);
    CHECK(animator.count() == 1);
    animator.tick(50);
    CHECK(value == doctest::Approx(25));
}

TEST_CASE("zero duration applies at once; cancelling by owner or target stops") {
    Animator animator;
    float a = 0;
    float b = 0;
    int owner = 0;
    animator.animate(&a, 10, 0, Easing::Linear, &owner);
    CHECK(a == doctest::Approx(10));
    animator.animate(&a, 20, 100, Easing::Linear, &owner);
    animator.animate(&b, 20, 100, Easing::Linear, nullptr);
    animator.cancel(&owner);
    animator.tick(100);
    CHECK(a == doctest::Approx(10));
    CHECK(b == doctest::Approx(20));
    animator.animate(&b, 0, 100, Easing::Linear, nullptr);
    animator.cancelTarget(&b);
    CHECK_FALSE(animator.active());
}

TEST_CASE("a delayed tween holds its start value until the delay has passed") {
    Animator animator;
    float value = 0;
    animator.animate(&value, 100, 100, Easing::Linear, nullptr, 35);
    animator.tick(30);
    CHECK(value == doctest::Approx(0));
    animator.tick(55);  // 50 ms into the tween
    CHECK(value == doctest::Approx(50));
    animator.tick(50);
    CHECK(value == doctest::Approx(100));
    CHECK_FALSE(animator.active());
}
