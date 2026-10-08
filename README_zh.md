* [English version](./README.md)
# WeActStudio.CANFDDevBoardV1_ESP32C5
![display](Images/1.png)

|目录名称|内容|
| :--:|:--:|
|Doc| 数据手册|
|Hardware| 硬件开发资料|
|Examples|示例|

## 硬件版本说明
```
**V1.0**
- 初始版本。
```

## 注意事项
```
Arduino开发环境：
- ESP32 C5开发板：`ESP32C5 Dev Module`
```

## 管脚说明
```
// OBD2
"CANL": 14,
"CANH": 6,
"VIN": 16,
"GND": 4/5,

// CAN
"RX": GPIO24,
"TX": GPIO23,

// Input Voltage
"VIN": GPIO1,
int analogVolts = analogReadMilliVolts(1);
int vin_Volts = analogVolts * ( 51 + 510 + 51 ) / 51;

// WS2812
"LED": GPIO27,

// KEY
"KEY": GPIO28,

// FPC-18Pin-0.5mm Connector
"PIN1": GND,
"PIN2": 3.3V,
"PIN3": GPIO10,
"PIN4": GPIO9,
"PIN5": GPIO8,
"PIN6": GPIO7,
"PIN7": GPIO6,
"PIN8": GPIO0,
"PIN9": GPIO3,
"PIN10": GPIO2,
"PIN11": GPIO26,
"PIN12": GPIO25,
"PIN13": GPIO11,
"PIN14": GPIO12,
"PIN15": GPIO4,
"PIN16": GPIO5,
"PIN17": 5V,
"PIN18": GND,
```

```
/*---------------------------------------
- WeAct Studio Official Link
- taobao: WeActStudio.taobao.com
- aliexpress 1: WeActStudio.aliexpress.com
- aliexpress 2: WeActStudioOne.aliexpress.com
- github: github.com/WeActStudio
- gitee: gitee.com/WeAct-TC
- blog: www.weact-tc.cn
---------------------------------------*/
```