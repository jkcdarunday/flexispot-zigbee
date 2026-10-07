#include "../firmware/flexispot_zigbee/desk_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

using namespace desk;
std::vector<uint8_t> packet(uint8_t type, std::vector<uint8_t> payload) {
  std::vector<uint8_t> frame = {0x9b, uint8_t(payload.size() + 4), type};
  frame.insert(frame.end(), payload.begin(), payload.end());
  uint16_t crc = crc16(frame.data() + 1, frame.size() - 1);
  frame.push_back(crc >> 8); frame.push_back(crc & 0xff); frame.push_back(0x9d);
  return frame;
}
bool parse(Parser &parser, const std::vector<uint8_t> &bytes, Event &event, uint32_t now = 0) {
  bool got = false;
  for (uint8_t byte : bytes) got = parser.feed(byte, now, event);
  return got;
}
int main() {
  // Independently recorded upstream wire packets, not generated expectations.
  const uint8_t recorded[][8] = {
    {0x9b,6,2,0x10,0,0xac,0xac,0x9d}, {0x9b,6,2,0,1,0xac,0x60,0x9d},
    {0x9b,6,2,4,0,0xac,0xa3,0x9d}, {0x9b,6,2,8,0,0xac,0xa6,0x9d},
    {0x9b,6,2,1,0,0xfc,0xa0,0x9d}, {0x9b,6,2,2,0,0x0c,0xa0,0x9d},
    {0x9b,6,2,0x20,0,0xac,0xb8,0x9d}, {0x9b,6,2,0,0,0x6c,0xa1,0x9d}
  };
  for (unsigned i = 0; i < 8; ++i) {
    uint8_t out[8]; commandPacket(static_cast<Command>(i), out);
    for (unsigned j = 0; j < 8; ++j) assert(out[j] == recorded[i][j]);
  }
  Parser parser; Event event;
  assert(parse(parser, packet(0x11, {}), event) && event.poll);
  assert(parse(parser, packet(0x12, {6,0x6d,0x3f}), event) && event.hasHeight && event.height == 150);
  assert(parse(parser, packet(0x12, {0x7d, uint8_t(0x7f|0x80),0x6d}), event) && event.hasHeight && event.height == 68.5f);
  assert(parse(parser, packet(0x12, {0,0x07,0x6f,0,0,0}), event) && event.hasHeight && event.height == 79);
  for (auto display : {std::vector<uint8_t>{0x40,0x40,0x40}, {0,0,0}, {0x79,6,6}, {0x6f,0x6f,0x6f}}) {
    assert(parse(parser, packet(0x12, display), event) && !event.hasHeight);
  }
  auto corrupted = packet(0x12, {6,0x6d,0x3f}); corrupted[3] ^= 1;
  assert(!parse(parser, corrupted, event));
  auto badEnd = packet(0x11, {}); badEnd.back() = 0;
  assert(!parse(parser, badEnd, event));
  // Noise, invalid lengths, truncated packets, and embedded framing bytes.
  parse(parser, {0,0x9b,0,0x9b,255}, event);
  assert(parse(parser, packet(0x11, {0x9b,0x9d}), event) && event.poll);
  parse(parser, {0x9b,7,0x12}, event, 1);
  assert(parse(parser, packet(0x11, {}), event, 200) && event.poll);
  for (unsigned i = 0; i < 100000; ++i) parser.feed(uint8_t(i * 73), i, event);
  assert(parse(parser, packet(0x11, {}), event, 100200) && event.poll);

  Controller controller;
  controller.begin(0);
  assert(!controller.request(Command::Up, 9999));
  controller.tick(10000);
  assert(controller.request(Command::Up, 10000));
  assert(!controller.wakeHigh());
  assert(controller.pollResponse() == Command::Release);
  controller.tick(10099); assert(!controller.wakeHigh());
  controller.tick(10100); assert(controller.wakeHigh());
  controller.tick(11199); assert(controller.pollResponse() == Command::Release);
  controller.tick(11200); assert(controller.pollResponse() == Command::Up);
  assert(!controller.request(Command::Down, 11201));
  assert(!controller.request(Command::Up, 11699));  // cannot extend the deadline
  controller.tick(11700); assert(controller.pollResponse() == Command::Release);
  assert(controller.request(Command::Stand, 11700));
  controller.tick(11800); controller.tick(12900);
  controller.tick(13899); assert(controller.pollResponse() == Command::Stand);
  controller.tick(13900); assert(controller.pollResponse() == Command::Release);
  controller.request(Command::Down, 14000);
  controller.request(Command::Release, 14001);
  controller.tick(20000); assert(controller.pollResponse() == Command::Release && controller.wakeHigh());
  // millis() wraparound must not prolong a keypress.
  controller.begin(UINT32_MAX - 9999);
  controller.tick(0); assert(controller.state() == Controller::State::Idle);
  controller.request(Command::Up, UINT32_MAX - 50);
  controller.tick(49); assert(controller.wakeHigh());
  controller.tick(1149); assert(controller.pollResponse() == Command::Up);
  controller.tick(1649); assert(controller.pollResponse() == Command::Release);
  puts("Desk protocol/state tests passed (including malformed frames and timer rollover).");
}
