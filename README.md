# Project5-Group7 (BMP Image Processing)

本仓库包含 BMP 图像处理小程序，支持缩放、旋转与镜像。代码均为 C 语言实现，适用于 8 位（灰度）与 24 位（彩色）无压缩 BMP。

## 功能概览

- **缩放**：`imgproc_scale.c`
  - 近邻插值（n）/双线性插值（l）/三次立方插值（c）
- **旋转**：`imgproc_rotate.c`
  - 0 / 90 / 180 / 270 度顺时针
- **镜像**：`imgproc_mirror.c`
  - 水平（-h）或垂直（-v）镜像

## 编译方式

### 使用 GCC（命令行）

```bash
gcc imgproc_scale.c -o imgproc_scale -lm
gcc imgproc_rotate.c -o imgproc_rotate
gcc imgproc_mirror.c -o imgproc_mirror
```

> `imgproc_scale.c` 使用了 `math.h`，需链接 `-lm`。

### 使用 Dev-C++

在 Dev-C++ 中分别打开每个 `.c` 文件进行编译运行即可。

## 运行示例

### 缩放

```bash
imgproc_scale -z -m n input.bmp 150 output.bmp
```

- `-z`：缩放
- `-m`：插值方法（n/l/c）
- `150`：缩放百分比（1-999）

### 旋转

```bash
imgproc_rotate -r input.bmp 90 output.bmp
```

- `-r`：旋转
- `90`：角度（0/90/180/270）

### 镜像

```bash
imgproc_mirror -m input.bmp -h output.bmp
```

- `-m`：镜像
- `-h`：水平镜像（`-v` 为垂直镜像）

## 说明

- 仅支持 **无压缩 BMP** 文件。
- 支持 **8 位（灰度）与 24 位（彩色）**。
