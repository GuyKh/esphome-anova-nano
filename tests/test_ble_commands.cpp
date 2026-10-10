#include <vector>
#include <cassert>
#include <cstdio>

// Copy of the functions from anova_nano.cpp (for testing purposes)
std::vector<uint8_t> encode_command(const std::vector<uint8_t> &buffer, bool append_zero = true) {
    std::vector<uint8_t> result;
    result.push_back(0);  // Start with 0
    size_t last_value = 0;
    size_t current_index = 1;
    
    auto reset_index = [&](bool is_end) {
        result[last_value] = static_cast<uint8_t>(current_index);
        last_value = result.size();
        if (is_end) {
            result.push_back(0);
        }
        current_index = 1;
    };
    
    for (size_t u = 0; u < buffer.size(); ++u) {
        if (buffer[u] == 0) {
            reset_index(true);
        } else {
            result.push_back(buffer[u]);
            current_index++;
            if (current_index == 255) {
                reset_index(true);
            }
        }
    }
    reset_index(false);
    
    if (append_zero) {
        result.push_back(0);
    }
    
    return result;
}

std::vector<uint8_t> create_command_array(uint8_t message_type, const std::vector<uint8_t> &value = {}) {
    // Based on pyanova-nano.commands.create_command_array
    std::vector<uint8_t> command = {
        0x03,  // ANOVA_DOMAIN_ID_CONFIG
        message_type             // Message type
    };
    
    // Add value if provided
    if (!value.empty()) {
        command.insert(command.end(), value.begin(), value.end());
    }
    
    return encode_command(command, true);
}

void test_encode_command_empty() {
    std::vector<uint8_t> input = {};
    std::vector<uint8_t> expected = {1, 0}; // [1,0]
    std::vector<uint8_t> actual = encode_command(input, true);
    assert(actual == expected);
}

void test_encode_command_simple() {
    std::vector<uint8_t> input = {3, 1};
    std::vector<uint8_t> expected = {2, 3, 1, 0}; // [2,3,1,0]
    std::vector<uint8_t> actual = encode_command(input, true);
    assert(actual == expected);
}

void test_encode_command_with_zeros() {
    std::vector<uint8_t> input = {0, 5, 0, 10};
    // Let's compute step by step:
    // Start: [0], last_value=0, current_index=1
    // u0: 0 -> reset_index(true): 
    //   result[0] = 1 -> [1], last_value=1, current_index=1 -> result=[1,0]
    // u1: 5 -> append 5 -> [1,0,5], current_index=2
    // u2: 0 -> reset_index(true):
    //   result[1] = 2 -> [1,2,5], last_value=2, current_index=1 -> result=[1,2,5,0]
    // u3: 10 -> append 10 -> [1,2,5,0,10], current_index=2
    // After loop: reset_index(false):
    //   result[2] = 2 -> [1,2,2,0,10], last_value=3, current_index=1
    // Then append 0 -> [1,2,2,0,10,1,0]
    std::vector<uint8_t> expected = {1, 2, 2, 0, 10, 1, 0};
    std::vector<uint8_t> actual = encode_command(input, true);
    assert(actual == expected);
}

void test_create_command_array_get_sensors() {
    // GET_SENSORS message type is 0x01 (from pyanova-nano)
    std::vector<uint8_t> expected = {2, 3, 1, 0}; // [2,3,1,0] as computed above
    std::vector<uint8_t> actual = create_command_array(0x01);
    assert(actual == expected);
}

void test_create_command_array_with_value() {
    // Let's test with a simple value: [0xFF]
    std::vector<uint8_t> value = {0xFF};
    std::vector<uint8_t> command_before_encode = {0x03, 0x02, 0xFF}; // domain id, message type (0x02 for example), value
    // Now we need to encode [0x03, 0x02, 0xFF]
    // Step by step:
    // Start: [0], last_value=0, current_index=1
    // u0: 3 -> [0,3], current_index=2
    // u1: 2 -> [0,3,2], current_index=2
    // u2: 255 -> [0,3,2,255], current_index=256 -> which triggers reset_index(true) because current_index==255
    //   reset_index(true): 
    //     result[0] = 3 -> [3,3,2,255], last_value=3, current_index=1 -> result=[3,3,2,255,0]
    // After loop: reset_index(false):
    //   result[3] = 1 -> [3,3,2,1,0], last_value=4, current_index=1
    // Then append 0 -> [3,3,2,1,0,1,0]
    std::vector<uint8_t> expected = {3, 3, 2, 1, 0, 1, 0};
    std::vector<uint8_t> actual = create_command_array(0x02, value);
    assert(actual == expected);
}

int main() {
    try {
        test_encode_command_empty();
        test_encode_command_simple();
        test_encode_command_with_zeros();
        test_create_command_array_get_sensors();
        test_create_command_array_with_value();
        printf("All tests passed!\n");
        return 0;
    } catch (const std::exception& e) {
        printf("Test failed: %s\n", e.what());
        return 1;
    }
}