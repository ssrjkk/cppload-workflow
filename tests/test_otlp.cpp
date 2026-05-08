#include <gtest/gtest.h>
#include "cppload/otel/exporter.hpp"
#include <thread>

TEST(OtlpExporterTest, Construct) {
    cppload::otel::TraceConfig cfg;
    cfg.endpoint = "http://localhost:4318";
    ASSERT_NO_THROW({
        cppload::otel::Tracer tracer(cfg);
    });
}

TEST(OtlpExporterTest, StartEndSpan) {
    cppload::otel::Tracer tracer;
    EXPECT_NO_THROW({
        tracer.start_span("test_span");
        tracer.add_attribute("key1", "value1");
        tracer.end_span();
    });
}

TEST(OtlpExporterTest, NestedSpans) {
    cppload::otel::Tracer tracer;
    tracer.start_span("parent");
    tracer.start_span("child");
    tracer.end_span(); // child
    tracer.end_span(); // parent
}

TEST(OtlpExporterTest, TraceIdNotEmpty) {
    cppload::otel::Tracer tracer;
    EXPECT_FALSE(tracer.trace_id().empty());
    EXPECT_EQ(tracer.trace_id().length(), 32); // 128-bit hex
}

TEST(OtlpExporterTest, AutoEndOnDestroy) {
    {
        cppload::otel::Tracer tracer;
        tracer.start_span("should_end");
        // Destructor should auto-end span
    }
}

TEST(OtlpExporterTest, MultipleSpans) {
    cppload::otel::Tracer tracer;
    for (int i = 0; i < 10; ++i) {
        tracer.start_span("span_" + std::to_string(i));
        tracer.add_attribute("idx", std::to_string(i));
        tracer.end_span();
    }
}
