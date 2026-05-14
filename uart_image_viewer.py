"""
OV2640 串口图像查看器
====================
从 STM32 串口接收 RGB565 图像数据并实时显示。

协议格式（与 main.c 中 OV2640_SendFrameToUART 对应）：
  [0xAA 0x55 0xAA 0x55] [W_H W_L] [H_H H_L] [S3 S2 S1 S0] [RGB565 数据...]

用法：
  python uart_image_viewer.py

依赖：
  pip install pyserial numpy opencv-python
"""

import sys
import time
import numpy as np
import serial
import cv2

# 4字节同步头
SYNC_PATTERN = bytes([0xAA, 0x55, 0xAA, 0x55])
HEADER_SIZE = 12  # 4 sync + 2 width + 2 height + 4 size
MAX_FRAME_BYTES = 512 * 1024


class FrameParser:
    def __init__(self):
        self.buffer = bytearray()
        self.frame_count = 0

    def feed(self, data):
        """喂入串口数据，返回解析出的图像或None"""
        self.buffer.extend(data)
        return self._try_parse()

    def _try_parse(self):
        """尝试从buffer中解析一帧完整图像"""
        while True:
            # 查找同步头
            idx = self.buffer.find(SYNC_PATTERN)
            if idx == -1:
                # 没找到同步头，保留最后3字节（可能是不完整的同步头）
                if len(self.buffer) > 3:
                    self.buffer = self.buffer[-3:]
                return None

            # 丢弃同步头之前的垃圾数据
            if idx > 0:
                self.buffer = self.buffer[idx:]

            # 检查是否有完整的帧头
            if len(self.buffer) < HEADER_SIZE:
                return None  # 等待更多数据

            # 解析帧头
            width = (self.buffer[4] << 8) | self.buffer[5]
            height = (self.buffer[6] << 8) | self.buffer[7]
            total_bytes = (self.buffer[8] << 24) | (self.buffer[9] << 16) | \
                          (self.buffer[10] << 8) | self.buffer[11]

            # 验证帧头合法性
            if not (1 <= width <= 800 and 1 <= height <= 600 and
                    total_bytes == width * height * 2 and
                    total_bytes <= MAX_FRAME_BYTES):
                # 无效帧头，跳过这个同步头，继续搜索下一个
                self.buffer = self.buffer[4:]
                continue

            # 检查是否有完整的帧数据
            frame_total = HEADER_SIZE + total_bytes
            if len(self.buffer) < frame_total:
                return None  # 等待更多数据

            # 提取像素数据
            pixel_data = bytes(self.buffer[HEADER_SIZE:frame_total])

            # 移除已处理的数据
            self.buffer = self.buffer[frame_total:]

            # 构建图像
            img = self._build_image(pixel_data, width, height)
            if img is not None:
                self.frame_count += 1
                return img

    def _build_image(self, pixel_data, width, height):
        try:
            rgb565 = np.frombuffer(pixel_data, dtype=np.uint16)
            if len(rgb565) != width * height:
                return None

            r = ((rgb565 >> 11) & 0x1F) * 255 // 31
            g = ((rgb565 >> 5) & 0x3F) * 255 // 63
            b = (rgb565 & 0x1F) * 255 // 31

            img = np.zeros((height, width, 3), dtype=np.uint8)
            img[:, :, 0] = b.reshape(height, width)
            img[:, :, 1] = g.reshape(height, width)
            img[:, :, 2] = r.reshape(height, width)
            return img
        except Exception:
            return None


def main():
    print("=" * 50)
    print("OV2640 串口图像查看器")
    print("=" * 50)

    try:
        import serial.tools.list_ports
        ports = serial.tools.list_ports.comports()
        if ports:
            print("\n可用串口:")
            for i, port in enumerate(ports):
                print(f"  {i+1}. {port.device} - {port.description}")
            print()
    except Exception:
        pass

    com_port = input("请输入串口名称 (例如 COM3): ").strip()
    baud_input = input("请输入波特率 (默认 921600): ").strip()
    baudrate = int(baud_input) if baud_input else 921600

    print(f"\n正在连接 {com_port} @ {baudrate} baud...")

    try:
        ser = serial.Serial(com_port, baudrate, timeout=0.1)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        print(f"成功打开 {com_port}")
        print("等待图像数据，按 'q' 退出，按 's' 保存图片...\n")
    except serial.SerialException as e:
        print(f"打开串口失败: {e}")
        input("\n按 Enter 键退出...")
        sys.exit(1)

    parser = FrameParser()
    last_frame_time = time.time()
    total_bytes_recv = 0

    cv2.namedWindow("OV2640 实时图像", cv2.WINDOW_NORMAL)

    try:
        while True:
            try:
                waiting = ser.in_waiting
            except serial.SerialException:
                print("\n串口断开")
                break

            if waiting > 0:
                data = ser.read(waiting)
                total_bytes_recv += len(data)
                result = parser.feed(data)

                if result is not None:
                    now = time.time()
                    fps_val = 1.0 / (now - last_frame_time) if (now - last_frame_time) > 0 else 0
                    last_frame_time = now

                    cv2.imshow("OV2640 实时图像", result)
                    cv2.resizeWindow("OV2640 实时图像",
                                     max(result.shape[1] * 2, 240),
                                     max(result.shape[0] * 2, 240))

                    print(f"[帧 {parser.frame_count}] {result.shape[1]}x{result.shape[0]}  "
                          f"FPS: {fps_val:.1f}  缓冲: {len(parser.buffer)} 字节")

            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                print("\n用户退出")
                break
            elif key == ord('s') and parser.frame_count > 0:
                filename = f"frame_{parser.frame_count:04d}.png"
                # 保存最后显示的帧需要额外逻辑，这里简单提示
                print(f"请使用截图保存")

    except KeyboardInterrupt:
        print("\n\n用户中断")
    except Exception as e:
        print(f"\n错误: {e}")
        import traceback
        traceback.print_exc()
    finally:
        if ser.is_open:
            ser.close()
            print("串口已关闭")
        cv2.destroyAllWindows()
        print(f"总共接收 {parser.frame_count} 帧, {total_bytes_recv} 字节")


if __name__ == "__main__":
    main()
