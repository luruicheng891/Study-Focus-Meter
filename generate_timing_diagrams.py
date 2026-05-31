#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
AI学习状态监测仪 - 传感器时序图生成脚本
生成 AHT20 和 BH1750 传感器的通信时序图 (PNG格式)
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
import numpy as np

# 设置中文字体支持
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'SimHei', 'Arial Unicode MS', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# 颜色定义
COLORS = {
    'scl': '#2196F3',      # 蓝色 - SCL
    'sda': '#4CAF50',       # 绿色 - SDA
    'ack': '#FF9800',      # 橙色 - ACK
    'data': '#9C27B0',      # 紫色 - 数据
    'label': '#333333',    # 深灰 - 标签
    'comment': '#666666',  # 浅灰 - 注释
    'arrow': '#F44336',    # 红色 - 箭头注释
    'bg': '#FAFAFA',       # 背景
    'box': '#E3F2FD',      # 盒子填充
}

def draw_i2c_signal(ax, y, signal_data, label, color, x_offset=0, height=0.8):
    """绘制I2C信号线 (SCL或SDA)"""
    ax.plot([x_offset, x_offset + len(signal_data)], [y, y], color=color, linewidth=2)
    ax.text(x_offset - 0.3, y, label, fontsize=10, fontweight='bold', color=color,
            verticalalignment='center', horizontalalignment='right')

def draw_bit_transition(ax, x, y_low, y_high, color='black'):
    """绘制单个位的电平变化"""
    ax.plot([x, x], [y_low, y_high], color=color, linewidth=1.5)

def draw_byte_box(ax, x, y, width, height, label, color='lightblue', text_color='black'):
    """绘制字节/数据框"""
    box = FancyBboxPatch((x, y), width, height, boxstyle="round,pad=0.02",
                         facecolor=color, edgecolor='gray', linewidth=1)
    ax.add_patch(box)
    ax.text(x + width/2, y + height/2, label, fontsize=8, ha='center', va='center',
            color=text_color, fontweight='bold')

def draw_arrow_annotation(ax, x1, y1, x2, y2, text, color=None):
    """绘制带文字的箭头注释"""
    if color is None:
        color = COLORS['arrow']
    ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                arrowprops=dict(arrowstyle='->', color=color, lw=1.5))
    ax.text((x1 + x2)/2, (y1 + y2)/2 + 0.15, text, fontsize=7, ha='center', color=color)

def draw_timing_diagram_aht20():
    """生成 AHT20 传感器时序图"""
    fig, axes = plt.subplots(4, 1, figsize=(16, 20))
    fig.suptitle('AHT20 温湿度传感器通信时序图', fontsize=16, fontweight='bold', y=0.98)

    # ========== 图1: AHT20 初始化时序 ==========
    ax = axes[0]
    ax.set_xlim(-1, 25)
    ax.set_ylim(-1, 10)
    ax.set_title('1. AHT20 初始化时序', fontsize=12, fontweight='bold', pad=10)

    # 绘制信号线
    scl_y, sda_y = 8, 6
    ax.plot([-0.5, 24.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2, label='SCL')
    ax.plot([-0.5, 24.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2, label='SDA')
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # 绘制START
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.text(0.5, 9.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    # 设备地址
    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x70', COLORS['box'])
    ax.annotate('SAD+W', xy=(x+0.75, sda_y+1.5), xytext=(x+0.75, 4),
                fontsize=8, ha='center', color=COLORS['comment'],
                arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 4
    ax.plot([x, x+0.8], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)
    ax.plot([x+0.8, x+0.8], [sda_y, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.text(x+0.4, sda_y-0.7, 'ACK', fontsize=7, ha='center', color=COLORS['ack'])

    # 命令 0xBE
    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0xBE', COLORS['box'])
    ax.text(x+0.75, 4, 'Init CMD', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 8
    ax.plot([x, x+0.8], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)
    ax.plot([x+0.8, x+0.8], [sda_y, scl_y], color=COLORS['scl'], linewidth=1.5)

    # 参数 0x08
    x = 10
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x08', COLORS['box'])
    ax.text(x+0.75, 4, '参数1: 使能校准', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 12
    ax.plot([x, x+0.8], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)
    ax.plot([x+0.8, x+0.8], [sda_y, scl_y], color=COLORS['scl'], linewidth=1.5)

    # 参数 0x00
    x = 14
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x00', COLORS['box'])
    ax.text(x+0.75, 4, '参数2: 保留', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 16
    ax.plot([x, x+0.8], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)
    ax.plot([x+0.8, x+0.8], [sda_y, scl_y], color=COLORS['scl'], linewidth=1.5)

    # STOP
    x = 18
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+2], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+2], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.5, 9.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 延迟标注
    x = 21
    ax.annotate('', xy=(x+3, sda_y+0.5), xytext=(x, sda_y+0.5),
                arrowprops=dict(arrowstyle='<->', color=COLORS['arrow'], lw=1.5))
    ax.plot([x, x], [sda_y+0.5, 2], color=COLORS['arrow'], linewidth=1, linestyle='--')
    ax.plot([x+3, x+3], [sda_y+0.5, 2], color=COLORS['arrow'], linewidth=1, linestyle='--')
    ax.text(x+1.5, 2.5, '等待 500ms', fontsize=9, ha='center', color=COLORS['arrow'], fontweight='bold')

    ax.axis('off')

    # ========== 图2: AHT20 读取状态时序 ==========
    ax = axes[1]
    ax.set_xlim(-1, 20)
    ax.set_ylim(-1, 10)
    ax.set_title('2. AHT20 读取状态时序', fontsize=12, fontweight='bold', pad=10)

    scl_y, sda_y = 8, 6
    ax.plot([-0.5, 19.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([-0.5, 19.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2)
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # START
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.text(0.5, 9.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    # SAD+R
    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x71', COLORS['box'])
    ax.text(x+0.75, 4, 'SAD+R', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 4
    ax.plot([x, x+0.8], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)
    ax.plot([x+0.8, x+0.8], [sda_y, scl_y], color=COLORS['scl'], linewidth=1.5)

    # 状态字节
    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 2, 1.2, 'Status\nByte', '#FFF3E0', COLORS['label'])
    ax.text(x+1, 4, 'AHT20直接发送\n1字节状态', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # NACK
    x = 9
    ax.plot([x, x+0.8], [sda_y+1, sda_y+1], color='gray', linewidth=3)
    ax.plot([x+0.8, x+0.8], [sda_y, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.text(x+0.4, sda_y+1.5, 'NACK', fontsize=7, ha='center', color='gray')

    # STOP
    x = 11
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+2], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+2], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.5, 9.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 状态位说明框
    x = 14
    box_text = '状态字节格式:\n[7] Busy    = 1: 正在测量\n[6] Reserved\n[5:3] Mode[2:0]\n[2] Reserved\n[1] Reserved\n[0] CAL = 1: 校准使能'
    props = dict(boxstyle='round', facecolor='#E8F5E9', edgecolor='#4CAF50', linewidth=2)
    ax.text(x, 7, box_text, fontsize=8, verticalalignment='top', bbox=props, family='monospace')

    ax.axis('off')

    # ========== 图3: AHT20 测量+读取数据时序 ==========
    ax = axes[2]
    ax.set_xlim(-1, 32)
    ax.set_ylim(-1, 14)
    ax.set_title('3. AHT20 触发测量 + 读取数据时序', fontsize=12, fontweight='bold', pad=10)

    scl_y, sda_y = 12, 10
    ax.plot([-0.5, 31.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([-0.5, 31.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2)
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # 发送测量命令阶段
    # START
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.text(0.5, 13.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    # SAD+W
    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x70', COLORS['box'])

    # ACK
    x = 4
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    # 0xAC
    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0xAC', COLORS['box'])
    ax.text(x+0.75, 9, '测量命令', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 8
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    # 0x33
    x = 10
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x33', COLORS['box'])
    ax.text(x+0.75, 9, '参数1', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 12
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    # 0x00
    x = 14
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x00', COLORS['box'])
    ax.text(x+0.75, 9, '参数2', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK + STOP
    x = 16
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)
    ax.plot([x+0.5, x+1], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(x+1, 13.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 延迟
    x = 18
    ax.annotate('', xy=(x+3, sda_y+0.5), xytext=(x, sda_y+0.5),
                arrowprops=dict(arrowstyle='<->', color=COLORS['arrow'], lw=1.5))
    ax.text(x+1.5, sda_y+1.5, '80ms', fontsize=9, ha='center', color=COLORS['arrow'], fontweight='bold')

    # 读取阶段
    # START
    x = 23
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)

    # SAD+R
    x = 25
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x71', COLORS['box'])

    # ACK
    x = 27
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    # 7字节数据
    data_y = 7
    labels = ['状态', '湿度H', '湿度M', '湿度L|T', '温度M', '温度L', 'CRC']
    for i, label in enumerate(labels):
        x = 29 + i * 0.7
        color = '#E3F2FD' if i == 0 else ('#FFF3E0' if i == 6 else '#E8F5E9')
        draw_byte_box(ax, x, data_y, 0.6, 0.8, label, color)

    # NACK + STOP
    x = 33
    ax.plot([x, x+0.5], [sda_y+1, sda_y+1], color='gray', linewidth=3)
    ax.plot([x+0.5, x+1], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+1], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1, 13.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 数据解析说明
    box_text = '''数据解析:
湿度(20bit) = [1][2]<<4 | [3]>>4
温度(20bit) = [3]&0x0F)<<16 | [4]<<8 | [5]

湿度 = raw_humi * 10000 / 2^20 (% * 100)
温度 = raw_temp * 20000 / 2^20 - 5000 (°C * 100)'''
    props = dict(boxstyle='round', facecolor='#FFF8E1', edgecolor='#FFC107', linewidth=2)
    ax.text(17, 4.5, box_text, fontsize=8, verticalalignment='top', bbox=props, family='monospace')

    ax.axis('off')

    # ========== 图4: AHT20 vs 标准I2C 区别 ==========
    ax = axes[3]
    ax.set_xlim(-1, 32)
    ax.set_ylim(-1, 12)
    ax.set_title('4. AHT20 与标准 I2C 寄存器访问的区别', fontsize=12, fontweight='bold', pad=10)

    # 标准I2C方式
    box_text_std = '''标准I2C读取 (如EEPROM):
1. START + SAD+W
2. 发送寄存器地址 (如 0x00)
3. START + SAD+R
4. 读取N字节 + NACK
5. STOP

特点: 先写寄存器地址，再读数据'''

    # AHT20方式
    box_text_aht = '''AHT20读取 (无寄存器地址):
1. START + SAD+W
2. 发送测量命令 0xAC 0x33 0x00
3. STOP
4. 等待80ms
5. START + SAD+R
6. 直接读取7字节 (含状态)
7. NACK + STOP

特点: 无需写寄存器地址，直接读取'''

    props_std = dict(boxstyle='round', facecolor='#E3F2FD', edgecolor='#2196F3', linewidth=2)
    ax.text(0, 10, '标准I2C方式', fontsize=10, fontweight='bold', color='#2196F3')
    ax.text(0, 9, box_text_std, fontsize=8, verticalalignment='top', bbox=props_std, family='monospace')

    props_aht = dict(boxstyle='round', facecolor='#E8F5E9', edgecolor='#4CAF50', linewidth=2)
    ax.text(0, 5, 'AHT20方式', fontsize=10, fontweight='bold', color='#4CAF50')
    ax.text(0, 4, box_text_aht, fontsize=8, verticalalignment='top', bbox=props_aht, family='monospace')

    ax.axis('off')

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig('AHT20_timing_diagrams.png', dpi=150, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close()
    print("AHT20 时序图已保存: AHT20_timing_diagrams.png")


def draw_timing_diagram_bh1750():
    """生成 BH1750 传感器时序图"""
    fig, axes = plt.subplots(4, 1, figsize=(16, 20))
    fig.suptitle('BH1750FVI 光照传感器通信时序图', fontsize=16, fontweight='bold', y=0.98)

    # ========== 图1: BH1750 上电+复位时序 ==========
    ax = axes[0]
    ax.set_xlim(-1, 30)
    ax.set_ylim(-1, 12)
    ax.set_title('1. BH1750 上电 + 复位时序', fontsize=12, fontweight='bold', pad=10)

    scl_y, sda_y = 10, 8
    ax.plot([-0.5, 29.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([-0.5, 29.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2)
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # 上电命令 0x01
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(0.5, 11.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x46', COLORS['box'])  # SAD+W
    ax.text(x+0.75, 7, 'SAD+W\n(0x23<<1)', fontsize=7, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    x = 4
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x01', COLORS['box'])
    ax.text(x+0.75, 7, 'Power On', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    x = 8
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 10
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+1.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1.5], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.2, 11.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 延迟 10ms
    x = 12
    ax.annotate('', xy=(x+2, sda_y+0.5), xytext=(x, sda_y+0.5),
                arrowprops=dict(arrowstyle='<->', color=COLORS['arrow'], lw=1.5))
    ax.text(x+1, sda_y+1.5, '10ms', fontsize=9, ha='center', color=COLORS['arrow'], fontweight='bold')

    # 复位命令 0x07
    x = 16
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(x+0.5, 11.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    x = 18
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x46', COLORS['box'])

    x = 20
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 22
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x07', COLORS['box'])
    ax.text(x+0.75, 7, 'Reset', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    x = 24
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 26
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+1.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1.5], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.2, 11.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    ax.axis('off')

    # ========== 图2: BH1750 连续测量模式 ==========
    ax = axes[1]
    ax.set_xlim(-1, 30)
    ax.set_ylim(-1, 12)
    ax.set_title('2. BH1750 连续高分辨率测量模式 (0x10)', fontsize=12, fontweight='bold', pad=10)

    scl_y, sda_y = 10, 8
    ax.plot([-0.5, 29.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([-0.5, 29.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2)
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # 发送模式命令
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(0.5, 11.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x46', COLORS['box'])

    x = 4
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x10', '#E8F5E9')
    ax.text(x+0.75, 7, '连续H-Res\n1lx, 120ms', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    x = 8
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 10
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+1.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1.5], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.2, 11.2, '★ STOP (必须)', fontsize=8, ha='center', color=COLORS['arrow'], fontweight='bold')

    # 测量周期
    x = 13
    ax.annotate('', xy=(x+5, sda_y+0.5), xytext=(x, sda_y+0.5),
                arrowprops=dict(arrowstyle='<->', color=COLORS['arrow'], lw=1.5))
    ax.plot([x, x], [sda_y+0.5, 6], color=COLORS['arrow'], linewidth=1, linestyle='--')
    ax.plot([x+5, x+5], [sda_y+0.5, 6], color=COLORS['arrow'], linewidth=1, linestyle='--')
    ax.text(x+2.5, 6.5, '测量周期 120ms', fontsize=9, ha='center', color=COLORS['arrow'], fontweight='bold')

    # 可读取标注
    ax.text(x+2.5, 5, '← 此时可读取数据 →', fontsize=9, ha='center', color='#4CAF50', fontweight='bold')

    # 模式说明框
    box_text = '''指令集:
• 0x10 连续H-Res:  1lx, 120ms
• 0x11 连续H-Res2: 0.5lx, 120ms
• 0x13 连续L-Res:  4lx, 16ms
• 0x20 单次H-Res:  1lx, 120ms (自动断电)
• 0x21 单次H-Res2: 0.5lx, 120ms
• 0x23 单次L-Res:  4lx, 16ms'''
    props = dict(boxstyle='round', facecolor='#FFF8E1', edgecolor='#FFC107', linewidth=2)
    ax.text(20, 9.5, box_text, fontsize=8, verticalalignment='top', bbox=props, family='monospace')

    ax.axis('off')

    # ========== 图3: BH1750 读取数据时序 ==========
    ax = axes[2]
    ax.set_xlim(-1, 25)
    ax.set_ylim(-1, 12)
    ax.set_title('3. BH1750 读取光照数据时序', fontsize=12, fontweight='bold', pad=10)

    scl_y, sda_y = 10, 8
    ax.plot([-0.5, 24.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([-0.5, 24.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2)
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # START
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(0.5, 11.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    # SAD+R
    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x47', COLORS['box'])
    ax.text(x+0.75, 7, 'SAD+R\n(0x23<<1|1)', fontsize=7, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 4
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    # 高字节
    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, 'MSB', '#E3F2FD')
    ax.text(x+0.75, 7, '光照高8位', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # ACK
    x = 8
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    # 低字节
    x = 10
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, 'LSB', '#E3F2FD')
    ax.text(x+0.75, 7, '光照低8位', fontsize=8, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    # NACK
    x = 12
    ax.plot([x, x+0.5], [sda_y+1, sda_y+1], color='gray', linewidth=3)
    ax.text(x+0.25, sda_y+1.5, 'NACK', fontsize=7, ha='center', color='gray')

    # STOP
    x = 14
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+1.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1.5], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.2, 11.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 计算公式框
    box_text = '''光照计算:
raw_lux = (MSB << 8) | LSB

H-Resolution Mode:
  lux = raw_lux / 1.2 * (69 / MTreg)
  (MTreg=69时) lux = raw_lux / 1.2

H-Resolution Mode2:
  lux = raw_lux / 1.2 * (69 / MTreg) / 2'''
    props = dict(boxstyle='round', facecolor='#E8F5E9', edgecolor='#4CAF50', linewidth=2)
    ax.text(17, 9.5, box_text, fontsize=8, verticalalignment='top', bbox=props, family='monospace')

    ax.axis('off')

    # ========== 图4: BH1750 MTreg 灵敏度调节 ==========
    ax = axes[3]
    ax.set_xlim(-1, 35)
    ax.set_ylim(-1, 12)
    ax.set_title('4. BH1750 MTreg 灵敏度调节时序', fontsize=12, fontweight='bold', pad=10)

    scl_y, sda_y = 10, 8
    ax.plot([-0.5, 34.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([-0.5, 34.5], [sda_y, sda_y], color=COLORS['sda'], linewidth=2)
    ax.text(-1.2, scl_y, 'SCL', fontsize=10, fontweight='bold', color=COLORS['scl'], va='center')
    ax.text(-1.2, sda_y, 'SDA', fontsize=10, fontweight='bold', color=COLORS['sda'], va='center')

    # 设置高位
    x = 0
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(0.5, 11.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    x = 2
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x46', COLORS['box'])

    x = 4
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 6
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x4X', '#FFF3E0')
    ax.text(x+0.75, 7, '设置高位\n0x40|MT[7:5]', fontsize=7, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    x = 8
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 10
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+1.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1.5], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.2, 11.2, '★ STOP', fontsize=8, ha='center', color=COLORS['arrow'], fontweight='bold')

    # 重新START
    x = 14
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.text(x+0.5, 11.2, 'START', fontsize=8, ha='center', color=COLORS['comment'])

    x = 16
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x46', COLORS['box'])

    x = 18
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 20
    draw_byte_box(ax, x, sda_y+0.5, 1.5, 1, '0x6X', '#FFF3E0')
    ax.text(x+0.75, 7, '设置低位\n0x60|MT[4:0]', fontsize=7, ha='center', color=COLORS['comment'],
            arrowprops=dict(arrowstyle='->', color=COLORS['comment']))

    x = 22
    ax.plot([x, x+0.5], [sda_y, sda_y], color=COLORS['ack'], linewidth=3)

    x = 24
    ax.plot([x, x+0.5], [sda_y, sda_y+1], color=COLORS['sda'], linewidth=1.5)
    ax.plot([x+0.5, x+0.5], [sda_y+1, scl_y], color=COLORS['scl'], linewidth=1.5)
    ax.plot([x+0.5, x+1.5], [scl_y, scl_y], color=COLORS['scl'], linewidth=2)
    ax.plot([x+0.5, x+1.5], [sda_y+1, sda_y+1], color=COLORS['sda'], linewidth=2)
    ax.text(x+1.2, 11.2, 'STOP', fontsize=8, ha='center', color=COLORS['comment'])

    # 示例说明
    box_text = '''MTreg 灵敏度调节:
• 范围: 31 ~ 254, 默认 69
• 公式: 灵敏度倍数 = MTreg / 69
• 测量时间 = 基准时间 × MTreg / 69

示例: 设置 MTreg = 138 (2倍灵敏度)
  高位: 0x40 | (138>>5) = 0x40 | 0x04 = 0x44
  低位: 0x60 | (138&0x1F) = 0x60 | 0x0A = 0x6A

注意: 每条命令后必须发送 STOP!'''
    props = dict(boxstyle='round', facecolor='#E3F2FD', edgecolor='#2196F3', linewidth=2)
    ax.text(27, 9.5, box_text, fontsize=7, verticalalignment='top', bbox=props, family='monospace')

    ax.axis('off')

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig('BH1750_timing_diagrams.png', dpi=150, bbox_inches='tight',
                facecolor='white', edgecolor='none')
    plt.close()
    print("BH1750 时序图已保存: BH1750_timing_diagrams.png")


if __name__ == '__main__':
    print("正在生成 AHT20 时序图...")
    draw_timing_diagram_aht20()

    print("正在生成 BH1750 时序图...")
    draw_timing_diagram_bh1750()

    print("\n完成! 生成了以下图片文件:")
    print("  1. AHT20_timing_diagrams.png")
    print("  2. BH1750_timing_diagrams.png")