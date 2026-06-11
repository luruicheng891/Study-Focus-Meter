# MAX9814 具有 AGC 和低噪声麦克风偏置电路的麦克风放大器

## 特性

MAX9814 是一款低成本、高品质麦克风放大器，内置自动增益控制 (AGC) 以及低噪声麦克风偏置。该器件集成低噪声前置放大器、可变增益放大器 (VGA)、输出放大器、麦克风偏压发生器以及 AGC 控制电路。

低噪声前置放大器的增益固定为 12dB，而 VGA 增益可以根据输出电压和 AGC 门限在 20dB 和 0dB 之间自动调节。输出放大器具有 8dB、18dB 和 28dB 三种可选增益。在没有压缩的条件下，放大器级联可使总增益达到 40dB、50dB 或 60dB。三态数字输入编程设置输出放大器的增益。外部电阻分压器控制 AGC 门限，单个电容可设置启动/释放时间。三态数字输入还可编程设置启动与释放时间的比，AGC 的保持时间固定值为 30ms。低噪声麦克风偏置发生器能为大多数驻极体麦克风提供偏压。

MAX9814 采用节省空间的 14 引脚 TDFN 封装。该器件规定工作在 \(-40^{\circ}C\) 至 \(+85^{\circ}C\) 扩展级温度范围。

- 自动增益控制 (AGC)
- 三种增益设置（40dB、50dB、60dB）
- 可编程启动时间
- 可编程启动与释放比
- 2.7V 至 5.5V 电源电压范围
- 低达 30nV/√Hz 的输入参考噪声密度
- 低达 0.04%（典型值）的 THD
- 低功耗关断模式
- 内部提供低噪声麦克风偏置，2V
- 采用节省空间的 14 引脚 TDFN（3mm × 3mm）封装
- \(-40^{\circ}C\) 至 \(+85^{\circ}C\) 扩展级温度范围

| PART         | TEMP RANGE   | PIN-PACKAGE |
| ------------ | ------------ | ----------- |
| MAX9814ETD+T | -40℃ to +85℃ | 14 TDFN-EP* |

*+表示无铅(Pb)/符合 RoHS 标准的封装。 T = 卷带包装。 EP = 裸焊盘。*

引脚配置在数据资料的最后给出。

<center>简化框图</center>

---

## ABSOLUTE MAXIMUM RATINGS

VDD to GND..............................................................-0.3V to +6V
All Other Pins to GND.................................-0.3V to (VDD + 0.3V)
Output Short-Circuit Duration.....................................Continuous
Continuous Current (MICOUT, MICBIAS).......................±100mA
All Other Pins....................................................................±20mA
Continuous Power Dissipation (TA = +70°C)
14-Pin TDFN-EP (derate 16.7mW/°C above +70°C)........................1481.5mW
Operating Temperature Range ...........................-40°C to +85°C
Junction Temperature......................................................+150°C
Lead Temperature (soldering, 10s) .................................+300°C
Bump Temperature (soldering) Reflow............................+235°C

---

## ELECTRICAL CHARACTERISTICS

(VDD = 3.3V, SHDN = VDD, CCT = 470nF, CCG = 2μF, GAIN = VDD, TA = TMIN to TMAX, unless otherwise specified. Typical values are at TA = +25°C.) (Note 1)

| PARAMETER                            | SYMBOL   | CONDITIONS                                                   | MIN    | TYP  | MAX    | UNITS |
| ------------------------------------ | -------- | ------------------------------------------------------------ | ------ | ---- | ------ | ----- |
| **GENERAL**                          |          |                                                              |        |      |        |       |
| Operating Voltage                    | VDD      | Guaranteed by PSRR test                                      | 2.7    |      | 5.5    | V     |
| Supply Current                       | IDD      |                                                              | 3.1    | 6    | mA     |       |
| Shutdown Supply Current              | ISHDN    |                                                              | 0.01   | 1    | μA     |       |
| Input-Referred Noise Density         | en       | BW = 20kHz, all gain settings                                | 30     |      | nV/√Hz |       |
| Output Noise                         |          | BW = 20kHz                                                   | 430    |      | μVRMS  |       |
| Signal-to-Noise Ratio                | SNR      | A-weighted                                                   | 64     |      | dB     |       |
| Dynamic Range                        | DR       | (Note 2)                                                     |        | 60   | dB     |       |
| Total Harmonic Distortion Plus Noise | THD+N    | fIN = 1kHz, BW = 20Hz to 20kHz, RL = 10kΩ, VTH = 1V (threshold = 2VP-P), VIN = 0.5mVRMS, VCT = 0V | 0.04   |      | %      |       |
|                                      |          | fIN = 1kHz, BW = 20Hz to 20kHz, RL = 10kΩ, VTH = 0.1V (threshold = 200mVP-P), VIN = 30mVRMS, VCT = 2V | 0.2    |      | %      |       |
| Amplifier Input BIAS                 | VIN      |                                                              | 1.14   | 1.23 | 1.32   | V     |
| Maximum Input Voltage                | VIN_MAX  | 1% THD                                                       | 100    |      | mVP-P  |       |
| Input Impedance                      | ZIN      |                                                              | 100    |      | kΩ     |       |
| Maximum Gain                         | A        | GAIN = VDD                                                   | 39.5   | 40   | 40.5   | dB    |
|                                      |          | GAIN = GND                                                   | 49.5   | 50   | 50.6   |       |
|                                      |          | GAIN = unconnected                                           | 59.5   | 60   | 60.5   |       |
| Minimum Gain                         |          | GAIN = VDD                                                   | 18.7   | 20   | 20.5   | dB    |
|                                      |          | GAIN = GND                                                   | 29.0   | 30   | 30.8   |       |
|                                      |          | GAIN = unconnected                                           | 38.7   | 40   | 40.5   |       |
| Maximum Output Level                 | VOUT_RMS | 1% THD+N, VTH = MICBIAS                                      | 0.707  |      | VRMS   |       |
| Regulated Output Level               |          | AGC enabled, VTH = 0.7V                                      | 1.26   | 1.40 | 1.54   | VP-P  |
| AGC Attack Time                      | tATTACK  | CCT = 470nF (Note 3)                                         | 1.1    |      | ms     |       |
| Attack/Release Ratio                 | A/R      | A/R = GND                                                    | 1:500  |      | ms/ms  |       |
|                                      |          | A/R = VDD                                                    | 1:2000 |      |        |       |
|                                      |          | A/R = unconnected                                            | 1:4000 |      |        |       |

| PARAMETER                       | SYMBOL         | CONDITIONS                                       | MIN                                   | TYP  | MAX       | UNITS |
| ------------------------------- | -------------- | ------------------------------------------------ | ------------------------------------- | ---- | --------- | ----- |
| **MICOUT**                      |                |                                                  |                                       |      |           |       |
| High Output Voltage             | VOH            | IOUT sourcing 1mA                                | 2.45                                  |      | V         |       |
| Low Output Voltage              | VOL            | IOUT sinking 1mA                                 | 3                                     |      | mV        |       |
| MICOUT Bias                     |                | MICOUT unconnected                               | 1.14                                  | 1.23 | 1.32      | V     |
| Output Impedance                | ZOUT           |                                                  | 50                                    |      | Ω         |       |
| Minimum Resistive Load          | RLOAD_MIN      |                                                  | 5                                     |      | kΩ        |       |
| Maximum Capacitive Drive        | CLOAD_MAX      |                                                  | 200                                   |      | pF        |       |
| Maximum Output Current          | IOUT_MAX       | 1% THD, RL = 500Ω                                | 12                                    |      | mA        |       |
| Output Short-Circuit Current    | ISC            |                                                  | 38                                    |      | mA        |       |
| Power-Supply Rejection Ratio    | PSRR           | AGC mode; VDD = 2.7V to 5.5V (Note 4)            | 35                                    | 50   | dB        |       |
|                                 |                | f = 217Hz, Vripple = 100mVp.p (Note 5)           |                                       | 55   |           |       |
|                                 |                | f = 1kHz, Vripple = 100mVp.p (Note 5)            |                                       | 52.5 |           |       |
|                                 |                | f = 10kHz, Vripple = 100mVp.p (Note 5)           |                                       | 43   |           |       |
| **MICROPHONE BIAS**             |                |                                                  |                                       |      |           |       |
| Microphone Bias Voltage         | VMICBIAS       | IMICBIAS = 0.5mA                                 | 1.84                                  | 2.0  | 2.18      | V     |
| Output Resistance               | RMICBIAS       | IMICBIAS = 1mA                                   | 1                                     |      | Ω         |       |
| Output Noise Voltage            | VMICBIAS_NOISE | IMICBIAS = 0.5mA, BW = 22Hz to 22kHz             | 5.5                                   |      | μVRMS     |       |
| Power-Supply Rejection Ratio    | PSRR           | DC, VDD = 2.7V to 5.5V                           | 70                                    | 80   | dB        |       |
|                                 |                | IMICBIAS = 0.5mA, Vripple = 100mVp.p, fIN = 1kHz |                                       | 71   |           |       |
| **TRILEVEL INPUTS (A/R, GAIN)** |                |                                                  |                                       |      |           |       |
| Tri-Level Input Leakage Current |                | A/R or GAIN = VDD                                | 0.5VDD / 180kΩ / 100kΩ / 50kΩ         |      | mA        |       |
|                                 |                | A/R or GAIN = GND                                | 0.5VDD / 180kΩ / 100kΩ / 100kΩ / 50kΩ |      |           |       |
| Input High Voltage              | VIH            |                                                  | VDD × 0.7                             |      |           | V     |
| Input Low Voltage               | VIL            |                                                  |                                       |      | VDD × 0.3 | V     |
| Shutdown Enable Time            | tON            |                                                  | 60                                    |      | ms        |       |
| Shutdown Disable Time           | tOFF           |                                                  | 40                                    |      | ms        |       |
| **DIGITAL INPUT (SHDN)**        |                |                                                  |                                       |      |           |       |
| SHDN Input Leakage Current      |                |                                                  | -1                                    |      | +1        | μA    |
| Input High Voltage              | VIH            |                                                  | 1.3                                   |      |           | V     |
| Input Low Voltage               | VIL            |                                                  |                                       |      | 0.5       | V     |
| **AGC THRESHOLD INPUT (TH)**    |                |                                                  |                                       |      |           |       |
| TH Input Leakage Current        |                |                                                  | -1                                    |      | +1        | μA    |

Note 1: Devices are production tested at TA = +25°C. Limits over temperature are guaranteed by design.
Note 2: Dynamic range is calculated using the EIAJ method. The input is applied at -60dBFS (0.707μVRMS), fIN = 1kHz.
Note 3: Attack time measured as time from AGC trigger to gain reaching 90% of its final value.
Note 4: CG is connected to an external DC voltage source, and adjusted until VMICOUT = 1.23V.
Note 5: CG connected to GND with 2.2μF.

---

## 典型工作特性

(VDD = 5V, CCT = 470nF, CCG = 2.2μF, VTH = VMICBIAS x 0.4, GAIN = VDD (40dB), AGC disabled, no load, RL = 10kΩ, COUT = 1μF, TA = +25°C, unless otherwise noted.)

*[图像：SUPPLY CURRENT vs. SUPPLY VOLTAGE]*
*[图像：SHUTDOWN CURRENT vs. SUPPLY VOLTAGE]*
*[图像：MICROPHONE BIAS VOLTAGE vs. MICROPHONE BIAS SOURCE CURRENT]*
*[图像：TOTAL HARMONIC DISTORTION PLUS NOISE vs. FREQUENCY]*
*[图像：TOTAL HARMONIC DISTORTION PLUS NOISE vs. OUTPUT VOLTAGE]*
*[图像：INPUT-REFERED NOISE vs. FREQUENCY]*

---

## 引脚说明

| 引脚 | 名称    | 功能                                                         |
| ---- | ------- | ------------------------------------------------------------ |
| 1    | CT      | 定时电容连接，将电容连接至 CT 控制 AGC 的启动时间和释放时间。 |
| 2    | SHDN    | 低电平有效关断控制。                                         |
| 3    | CG      | 放大器直流失调调节，连接一只 2.2μF 的电容至 GND，确保输出端零失调。 |
| 4,11 | N.C.    | 无连接，接 GND。                                             |
| 5    | VDD     | 电源，采用一只 1μF 电容旁路至 GND。                          |
| 6    | MICOUT  | 放大器输出。                                                 |
| 7    | GND     | 地。                                                         |
| 8    | MICIN   | 麦克风放大器同相输入。                                       |
| 9    | A/R     | 三态启动与释放比选择，控制 AGC 电路的启动时间与释放时间比：<br>A/R = GND：启动/释放比为 1:500<br>A/R = VDD：启动/释放比为 1:2000<br>A/R = 悬空：启动/释放比为 1:4000 |
| 10   | GAIN    | 三态放大器增益控制：<br>GAIN = VDD，增益设置为 40dB。<br>GAIN = GND，增益设置为 50dB。<br>GAIN = 悬空，无压缩增益设置为 60dB。 |
| 12   | BIAS    | 放大器偏置，采用一只 0.47μF 的电容旁路至 GND。               |
| 13   | MICBIAS | 麦克风偏置输出。                                             |
| 14   | TH      | AGC 门限控制，TH 电压设置增益控制门限。将 TH 连接至 MICBIAS，禁止 AGC。 |
| —    | EP      | 裸焊盘，将 TDFN 封装的 EP 连接至 GND。                       |

---

## 详细说明

MAX9814 是一款低成本、高品质麦克风放大器，内置自动增益控制 (AGC) 以及低噪声麦克风偏置。MAX9814 是由低噪声前置放大器、可变增益放大器 (VGA)、输出放大器、麦克风偏置发生器以及 AGC 控制电路等多个不同电路组成。

内部麦克风偏置发生器提供 2V 的偏压，适用于大多数驻极体电容式麦克风。MAX9814 分为三级，对输入进行放大。在第一级，输入通过增益为 12dB 的低噪声前置放大器进行缓冲和放大；第二级则由 AGC 控制的 VGA 组成，VGA/AGC 组合能够使增益在 20dB 与 0dB 之间变化；输出放大器是最后一级，具有 8dB、18dB、20dB 三个不同的固定增益，可通过一个三态逻辑输人编程设置。AGC 无压缩时，MAX9814 能够提供 40dB、50dB 或 60dB 的增益。

## 自动增益控制 (AGC)

不具备 AGC 的器件在输人增益过大时，输出将会出现削波；而在输人增益过大时，AGC 能够避免输出削波。图 1 所示为增益过大的麦克风输人在具有 AGC 和不带 AGC 的情况下的比较。

MAX9814 的 AGC 对增益进行控制，首先检测输出电压是否超过预设门限。随后，通过可选的时间常数降低麦克风放大器增益，以修正过大的输出电压幅值。这一过程称为启动时间。当输出信号幅值降低后，增益在很短时间内保持衰减状态，随后输出信号缓慢增加到正常值。该过程称为保持和释放时间。放大器调节输人信号的速度由外部定时电容 CCT 和 A/R 端电压设置。AGC 门限可通过 VTH 调节。增益衰减量为输人信号幅值的函数，最大 AGC 衰减为 20dB。图 2 给出了输人突然超出预设门限时，对输出启动时间、保持时间和释放时间的影响。

如果配置的启动时间和释放时间响应太快，增益随信号动态变化而快速调节，常常会产生类似“砰然”声 (pumping) 或“喘息”声 (breathing) 的音频噪声。调节 AGC 的时间常数使其与声源匹配，从而达到最佳效果。对于那些以 CD 音乐为主要音源的应用来说，推荐启动时间为 160us，释放时间为 80ms。通常情况下，音乐播放设备要比语音或电影等设备需要更短的释放时间。

*[图像：图 1. 带有 AGC 和没有 AGC 的麦克风输入]*
*[图像：图 2. 输入突然超过 AGC 门限]*

### 启动时间

启动时间是指当输入信号超过门限电平后，AGC 降低增益所需的时间。增益在启动时间内以指数形式衰减，定义为一个时间常数。该时间常数为 2400 x CCT 秒（其中 CCT 是外部定时电容）：

- 选取较短的启动时间，以保证 AGC 快速响应瞬态信号，例如击鼓声（音乐）或枪击声（DVD）。
- 选用较长的启动时间，AGC 将忽略瞬时峰值，只有当声响明显增加时才降低增益。瞬时峰值并不被衰减，但较弱的声音将被衰减。这样可从音量上降低响声，使动态范围最大化。

### 保持时间

保持时间是指信号降到门限以下、释放过程开始以前的延迟。保持时间内部设置为 30ms，并且不可调。当信号超过门限，重新进入启动阶段时，保持时间终止。

### 释放时间

释放时间是指信号跌落至门限以下，并且经过 30ms 的保持时间之后，增益回到其正常水平所需的时间。释放时间定义为当输入信号跌落至 TH 门限以下，并且经过 30ms 的保持时间之后，增益从 20dB 压缩释放到正常增益的 10% 的时间。释放时间可调，其最小值为 25ms。释放时间由 CCT 设置的启动时间以及利用 A/R（如表 1 所示）设置的启动/释放时间比确定：

- 采用小比值，使 AGC 的速度达到最大。
- 采用大比值，使音质达到最佳，防止 AGC 重复调节短时间内超出门限的信号。

### AGC 输出门限

激活 AGC 工作的输出门限可通过外部电阻分压器调节。完成对分压器的设置后，AGC 将降低增益，使输出电压与 TH 输入端设置的电压相匹配。

### 麦克风偏置

MAX9814 由内部提供低噪声麦克风偏置电压，可驱动大多数驻极体电容式麦克风。调节麦克风偏置至 2V，以保证进入低噪声前置放大器的输入信号不被箱位到地。

---

## 应用信息

### 设置启动时间和释放时间

启动时间和释放时间分别由 CT 和 GND 之间的电容以及 A/R 的逻辑状态（表 1）决定。A/R 为三态逻辑输入，可设置启动与释放时间比。

**表 1. 启动与释放比**

| A/R         | ATTACK/RELEASE RATIO |
| ----------- | -------------------- |
| GND         | 1:500                |
| VDD         | 1:2000               |
| Unconnected | 1:4000               |

根据表 2 所列的相应电容，可以选择启动时间和释放时间。

**表 2. 启动-释放时间**

| CCT   | tATTACK (ms) | tRELEASE (ms) |           |                   |
| ----- | ------------ | ------------- | --------- | ----------------- |
|       |              | A/R = GND     | A/R = VDD | A/R = UNCONNECTED |
| 22nF  | 0.05         | 25            | 100       | 200               |
| 47nF  | 0.11         | 55            | 220       | 440               |
| 68nF  | 0.16         | 80            | 320       | 640               |
| 100nF | 0.24         | 120           | 480       | 960               |
| 220nF | 0.53         | 265           | 1060      | 2120              |
| 470nF | 1.1          | 550           | 2200      | 4400              |
| 680nF | 1.63         | 815           | 3260      | 6520              |
| 1μF   | 2.4          | 1200          | 4800      | 9600              |

### 设置 AGC 门限

设置 AGC 门限若要设置麦克风输出箱位时的输出电压门限，应在 MICBIAS 和地之间连接外部电阻分压器，电阻分压器输出连接到 TH。电压 VTH 可确定输出箱位时的峰值电压门限。此时，输出端的最大信号摆幅为 VTH 的 2 倍，并保持不变，直到输入信号幅值衰减为止。若要禁止 AGC，可将 TH 连接至 MICBIAS。

### 麦克风偏置电阻

MICBIAS 可源出 20mA 的电流。选择适当的 RMICBIAS，从而为驻极体麦克风提供所需要的偏置电流。一般来说，2.2kΩ 的阻值对于典型灵敏度的麦克风已经足够了。关于偏置电阻的选择，请参考麦克风数据资料。

### 偏置电容

MAX9814 的 BIAS 输出在内部经过缓冲，提供低噪声偏压。采用一只 470nF 的电容将 BIAS 旁路至地。

### 输入电容

输入电容麦克风放大器的输入交流耦合电容 CIN 和输入阻抗 RIN 组成了一个高通滤波器，可滤除输入信号中的所有直流偏置（参见典型应用电路/功能框图）。CIN 可防止输入信号源的直流成分出现在放大器的输出。假设输入信号源阻抗为零，则高通滤波器的 -3dB 点为：

\[
f_{-3dB\_IN} = \frac{1}{2\pi \times R_{IN} \times C_{IN}}
\]

选择适当的 CIN 使 f_{-3dB_IN} 远低于敏感频率。f_{-3dB_IN} 设置过高，会影响放大器的低频响应，选择低电压系数的电介质电容。对于交流耦合电容来说，铝电解电容、钽电容或薄膜电介质电容都是很好的选择。高电压系数的电容，诸如陶瓷电容（非 COG 电介质），会加剧低频失真。

### 输出电容

MAX9814 的输出偏置在 1.23V，若要消除直流失调，应采用交流耦合电容（COUT）。考虑到下一级的输入阻抗 (RL)，COUT 和 RL 组成高通滤波器。假设输出阻抗为零，高通滤波器的 -3dB 点为：

\[
f_{-3dB\_OUT} = \frac{1}{2\pi \times R_{L} \times C_{OUT}}
\]

### 关断

MAX9814 具有低功耗关断模式。当 SHDN 为低电平时，电源电流跌落至 0.01μA，输出进入高阻状态，麦克风的偏置电流关断。驱动 SHDN 为高电平，使能放大器。请勿将 SHDN 悬空。

### 电源旁路与 PCB 布局

采用一只 0.1μF 的电容将电源旁路至地。缩短引线长度可降低寄生电容，外部元件应尽可能靠近器件放置，推荐选用表贴元件。在同时具有模拟地和数字地的系统中，MAX9814 的地与模拟地相连。

---

## 典型应用电路/功能框图

*[图像：典型应用电路/功能框图]*
*THE DEVICE HAS BEEN CONFIGURED WITH AN ATTACK TIME OF 1.1ms, 40dB GAIN, AND AN ATTACK-AND-RELEASE RATIO OF 1:500.*

---

## 引脚配置

PROCESS: BiCMOS

*[图像：引脚配置]*

---

## 封装信息

如需最近的封装外形信息和焊盘布局，请查询 china.maxim-ic.com/packages。

| 封装类型   | 封装编码 | 文档编号 |
| ---------- | -------- | -------- |
| 14 TDFN-EP | T1433-2  | 21-0137  |

### 封装外形尺寸

| SYMBOL | MIN.      | MAX. |
| ------ | --------- | ---- |
| A      | 0.70      | 0.80 |
| D      | 2.90      | 3.10 |
| E      | 2.90      | 3.10 |
| A1     | 0.00      | 0.05 |
| L      | 0.20      | 0.40 |
| k      | 0.25 MIN. |      |
| A2     | 0.20 REF. |      |

| PKG. CODE | N    | D2        | E2        | e        | JEDEC SPEC     | b         | [(N/2)-1] x e |
| --------- | ---- | --------- | --------- | -------- | -------------- | --------- | ------------- |
| T633-26   | 6    | 1.50±0.10 | 2.30±0.10 | 0.95 BSC | MO229 / WEEA   | 0.40±0.05 | 1.90 REF      |
| T833-28   | 8    | 1.50±0.10 | 2.30±0.10 | 0.65 BSC | MO229 / WEEA   | 0.30±0.05 | 1.95 REF      |
| T833-38   | 8    | 1.50±0.10 | 2.30±0.10 | 0.65 BSC | MO229 / WEEA   | 0.30±0.05 | 1.95 REF      |
| T1033-110 | 10   | 1.50±0.10 | 2.30±0.10 | 0.50 BSC | MO229 / WEEA-3 | 0.25±0.05 | 2.00 REF      |
| T1033-210 | 10   | 1.50±0.10 | 2.30±0.10 | 0.50 BSC | MO229 / WEEA-3 | 0.25±0.05 | 2.00 REF      |
| T1433-114 | 14   | 1.70±0.10 | 2.30±0.10 | 0.40 BSC | - - - -        | 0.20±0.05 | 2.40 REF      |
| T1433-214 | 14   | 1.70±0.10 | 2.30±0.10 | 0.40 BSC | - - - -        | 0.20±0.05 | 2.40 REF      |

NOTES:
1. ALL DIMENSIONS ARE IN mm. ANGLES IN DEGREES.
2. COPLANARITY SHALL NOT EXCEED 0.08 mm.
3. WARPAGE SHALL NOT EXCEED 0.10 mm.
4. PACKAGE LENGTH/PACKAGE WIDTH ARE CONSIDERED AS SPECIAL CHARACTERISTIC(S).
5. DRAWING CONFORMS TO JEDEC MO229, EXCEPT DIMENSIONS "D2" AND "E2", AND T1433-1 & T1433-2.
6. "N" IS THE TOTAL NUMBER OF LEADS.
7. NUMBER OF LEADS SHOWN ARE FOR REFERENCE ONLY.
8. MARKING IS FOR PACKAGE ORIENTATION REFERENCE ONLY.

TITLE: PACKAGE OUTLINE, 6,8,10 & 14L, TDFN, EXPOSED PAD, 3×3×0.80 mm
APPROVAL DOCUMENT CONTROL NO. 21-0137 REV. 21

---

## 修订历史

| 修订次数 | 修订日期 | 说明                                                         | 修改页      |
| -------- | -------- | ------------------------------------------------------------ | ----------- |
| 最初版本 | 03/07    | 最初版本。                                                   | —           |
| 1        | 12/09    | 更新了定购信息、Absolute Maximum Ratings、引脚说明和引脚配置部分，为 TDFN 封装添加了 EP 相关内容。 | 1,2,6,11    |
| 2        | 26/09    | 删除了 UCSP 封装的相关内容。                                 | 1,2,6,11,12 |