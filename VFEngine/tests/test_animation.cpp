#include <doctest.h>
#include <animator/AnimatorTypes.hpp>
#include <string>

// ============================================================
// VK-1090: Animation system unit tests
// ============================================================

TEST_SUITE("Animation") {

// ---- AnimatorRuntimeParameters ----

TEST_CASE("AnimatorRuntimeParameters: setFloat/getFloat with default") {
    animator::AnimatorRuntimeParameters params;
    CHECK(params.getFloat("speed") == doctest::Approx(0.0f));
    params.setFloat("speed", 2.5f);
    CHECK(params.getFloat("speed") == doctest::Approx(2.5f));
    CHECK(params.getFloat("missing", 7.0f) == doctest::Approx(7.0f));
}

TEST_CASE("AnimatorRuntimeParameters: setInt/getInt") {
    animator::AnimatorRuntimeParameters params;
    CHECK(params.getInt("combo") == 0);
    params.setInt("combo", 3);
    CHECK(params.getInt("combo") == 3);
    CHECK(params.getInt("missing", -1) == -1);
}

TEST_CASE("AnimatorRuntimeParameters: setBool/getBool") {
    animator::AnimatorRuntimeParameters params;
    CHECK(params.getBool("grounded") == false);
    params.setBool("grounded", true);
    CHECK(params.getBool("grounded") == true);
    params.setBool("grounded", false);
    CHECK(params.getBool("grounded") == false);
}

TEST_CASE("AnimatorRuntimeParameters: setTrigger/getTrigger/resetTrigger") {
    animator::AnimatorRuntimeParameters params;
    CHECK(params.getTrigger("jump") == false);
    params.setTrigger("jump");
    CHECK(params.getTrigger("jump") == true);
    params.resetTrigger("jump");
    CHECK(params.getTrigger("jump") == false);
    // resetTrigger on non-existent key should not crash
    params.resetTrigger("nonexistent");
}

// ---- evaluateCondition ----

TEST_CASE("evaluateCondition: Greater - true and false cases") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("speed", 5.0f);

    animator::TransitionCondition cond;
    cond.parameterName = "speed";
    cond.op = animator::ComparisonOperator::Greater;
    cond.value = 3.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 5.0f;
    CHECK(animator::evaluateCondition(cond, params) == false);

    cond.value = 10.0f;
    CHECK(animator::evaluateCondition(cond, params) == false);
}

TEST_CASE("evaluateCondition: Less") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("health", 2.0f);

    animator::TransitionCondition cond;
    cond.parameterName = "health";
    cond.op = animator::ComparisonOperator::Less;
    cond.value = 5.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 1.0f;
    CHECK(animator::evaluateCondition(cond, params) == false);
}

TEST_CASE("evaluateCondition: GreaterEqual") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("stamina", 5.0f);

    animator::TransitionCondition cond;
    cond.parameterName = "stamina";
    cond.op = animator::ComparisonOperator::GreaterEqual;

    cond.value = 5.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 4.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 6.0f;
    CHECK(animator::evaluateCondition(cond, params) == false);
}

TEST_CASE("evaluateCondition: LessEqual") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("stamina", 5.0f);

    animator::TransitionCondition cond;
    cond.parameterName = "stamina";
    cond.op = animator::ComparisonOperator::LessEqual;

    cond.value = 5.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 6.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 4.0f;
    CHECK(animator::evaluateCondition(cond, params) == false);
}

TEST_CASE("evaluateCondition: Equal") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("blend", 1.0f);

    animator::TransitionCondition cond;
    cond.parameterName = "blend";
    cond.op = animator::ComparisonOperator::Equal;
    cond.value = 1.0f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 0.5f;
    CHECK(animator::evaluateCondition(cond, params) == false);
}

TEST_CASE("evaluateCondition: NotEqual") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("blend", 1.0f);

    animator::TransitionCondition cond;
    cond.parameterName = "blend";
    cond.op = animator::ComparisonOperator::NotEqual;
    cond.value = 0.5f;
    CHECK(animator::evaluateCondition(cond, params) == true);

    cond.value = 1.0f;
    CHECK(animator::evaluateCondition(cond, params) == false);
}

// ---- evaluateAllConditions ----

TEST_CASE("evaluateAllConditions: all true returns true") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("speed", 5.0f);
    params.setFloat("health", 2.0f);

    std::vector<animator::TransitionCondition> conditions;

    animator::TransitionCondition c1;
    c1.parameterName = "speed";
    c1.op = animator::ComparisonOperator::Greater;
    c1.value = 3.0f;
    conditions.push_back(c1);

    animator::TransitionCondition c2;
    c2.parameterName = "health";
    c2.op = animator::ComparisonOperator::Less;
    c2.value = 5.0f;
    conditions.push_back(c2);

    CHECK(animator::evaluateAllConditions(conditions, params) == true);
}

TEST_CASE("evaluateAllConditions: one false returns false") {
    animator::AnimatorRuntimeParameters params;
    params.setFloat("speed", 5.0f);
    params.setFloat("health", 10.0f);

    std::vector<animator::TransitionCondition> conditions;

    animator::TransitionCondition c1;
    c1.parameterName = "speed";
    c1.op = animator::ComparisonOperator::Greater;
    c1.value = 3.0f;
    conditions.push_back(c1);

    animator::TransitionCondition c2;
    c2.parameterName = "health";
    c2.op = animator::ComparisonOperator::Less;
    c2.value = 5.0f;
    conditions.push_back(c2);

    CHECK(animator::evaluateAllConditions(conditions, params) == false);
}

// ---- String <-> Enum roundtrips ----

TEST_CASE("String/Enum roundtrip: parameterTypeToString and stringToParameterType") {
    CHECK(std::string(animator::parameterTypeToString(animator::AnimatorParameterType::Float)) == "Float");
    CHECK(std::string(animator::parameterTypeToString(animator::AnimatorParameterType::Int)) == "Int");
    CHECK(std::string(animator::parameterTypeToString(animator::AnimatorParameterType::Bool)) == "Bool");
    CHECK(std::string(animator::parameterTypeToString(animator::AnimatorParameterType::Trigger)) == "Trigger");

    CHECK(animator::stringToParameterType("Float") == animator::AnimatorParameterType::Float);
    CHECK(animator::stringToParameterType("Int") == animator::AnimatorParameterType::Int);
    CHECK(animator::stringToParameterType("Bool") == animator::AnimatorParameterType::Bool);
    CHECK(animator::stringToParameterType("Trigger") == animator::AnimatorParameterType::Trigger);

    // Roundtrip
    for (auto type : {animator::AnimatorParameterType::Float, animator::AnimatorParameterType::Int,
                      animator::AnimatorParameterType::Bool, animator::AnimatorParameterType::Trigger}) {
        CHECK(animator::stringToParameterType(animator::parameterTypeToString(type)) == type);
    }
}

TEST_CASE("String/Enum roundtrip: comparisonOperatorToString and stringToComparisonOperator") {
    CHECK(std::string(animator::comparisonOperatorToString(animator::ComparisonOperator::Greater)) == ">");
    CHECK(std::string(animator::comparisonOperatorToString(animator::ComparisonOperator::Less)) == "<");
    CHECK(std::string(animator::comparisonOperatorToString(animator::ComparisonOperator::GreaterEqual)) == ">=");
    CHECK(std::string(animator::comparisonOperatorToString(animator::ComparisonOperator::LessEqual)) == "<=");
    CHECK(std::string(animator::comparisonOperatorToString(animator::ComparisonOperator::Equal)) == "==");
    CHECK(std::string(animator::comparisonOperatorToString(animator::ComparisonOperator::NotEqual)) == "!=");

    CHECK(animator::stringToComparisonOperator(">") == animator::ComparisonOperator::Greater);
    CHECK(animator::stringToComparisonOperator("<") == animator::ComparisonOperator::Less);
    CHECK(animator::stringToComparisonOperator(">=") == animator::ComparisonOperator::GreaterEqual);
    CHECK(animator::stringToComparisonOperator("<=") == animator::ComparisonOperator::LessEqual);
    CHECK(animator::stringToComparisonOperator("==") == animator::ComparisonOperator::Equal);
    CHECK(animator::stringToComparisonOperator("!=") == animator::ComparisonOperator::NotEqual);

    // Roundtrip
    for (auto op : {animator::ComparisonOperator::Greater, animator::ComparisonOperator::Less,
                    animator::ComparisonOperator::GreaterEqual, animator::ComparisonOperator::LessEqual,
                    animator::ComparisonOperator::Equal, animator::ComparisonOperator::NotEqual}) {
        CHECK(animator::stringToComparisonOperator(animator::comparisonOperatorToString(op)) == op);
    }
}

} // TEST_SUITE
