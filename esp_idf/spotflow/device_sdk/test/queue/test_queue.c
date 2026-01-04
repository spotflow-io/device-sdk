#include "test_common.h"
#include "test_wrapper.h"

static void queue_setup(void)
{
    spotflow_queue_init();
}

/* ---------------- BASIC TEST ---------------- */

static void test_queue_single_push_pop_impl(void)
{
    const uint8_t data[] = { 0x10, 0x20, 0x30 };
    queue_msg_t msg = {0};

    spotflow_queue_push((uint8_t *)data, sizeof(data));

    TEST_SPOTFLOW_ASSERT_TRUE(spotflow_queue_read(&msg));
    TEST_SPOTFLOW_ASSERT_EQUAL(sizeof(data), msg.len);
    TEST_SPOTFLOW_ASSERT_EQUAL_UINT8_ARRAY(data, msg.ptr, msg.len);

    spotflow_queue_free(&msg);
}

static void test_queue_fifo_impl(void)
{
    const uint8_t a[] = { 1 };
    const uint8_t b[] = { 2 };
    const uint8_t c[] = { 3 };

    queue_msg_t msg;

    spotflow_queue_push((uint8_t *)a, sizeof(a));
    spotflow_queue_push((uint8_t *)b, sizeof(b));
    spotflow_queue_push((uint8_t *)c, sizeof(c));

    spotflow_queue_read(&msg);
    TEST_SPOTFLOW_ASSERT_EQUAL(1, msg.ptr[0]);
    spotflow_queue_free(&msg);

    spotflow_queue_read(&msg);
    TEST_SPOTFLOW_ASSERT_EQUAL(2, msg.ptr[0]);
    spotflow_queue_free(&msg);

    spotflow_queue_read(&msg);
    TEST_SPOTFLOW_ASSERT_EQUAL(3, msg.ptr[0]);
    spotflow_queue_free(&msg);
}

static void test_queue_empty_read_impl(void)
{
    queue_msg_t msg;
    bool ret = spotflow_queue_read(&msg);
    TEST_SPOTFLOW_ASSERT_FALSE(ret);
}

static void test_queue_overflow_impl(void)
{
    uint8_t dummy = 0xAA;
    queue_msg_t msg;

    for (int i = 0; i < CONFIG_SPOTFLOW_MESSAGE_QUEUE_SIZE + 2; i++) {
        spotflow_queue_push(&dummy, sizeof(dummy));
    }

    /* Behavior depends on implementation:
       - If overwrite → expect last values
       - If drop → expect limited reads
    */

    int count = 0;
    while (spotflow_queue_read(&msg)) {
        count++;
        spotflow_queue_free(&msg);
    }

    TEST_SPOTFLOW_ASSERT_LESS_OR_EQUAL(CONFIG_SPOTFLOW_MESSAGE_QUEUE_SIZE, count);
}

/* ---------------- TEST CASES ---------------- */

TEST_CASE("spotflow queue: single push pop", "[spotflow][queue]")
{
    queue_setup();
    test_queue_single_push_pop_impl();
}

TEST_CASE("spotflow queue: fifo order", "[spotflow][queue]")
{
    queue_setup();
    test_queue_fifo_impl();
}

TEST_CASE("spotflow queue: empty read", "[spotflow][queue]")
{
    queue_setup();
    test_queue_empty_read_impl();
}

TEST_CASE("spotflow queue: overflow", "[spotflow][queue]")
{
    queue_setup();
    test_queue_overflow_impl();
}
