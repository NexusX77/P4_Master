idf_component_register(
    SRC_DIRS     "."          # 编译 main/ 下所有 .c（main.c、app_wifi.c）
    INCLUDE_DIRS "."          # 导出 main/ 头文件路径（app_wifi.h 才能被找到）
    REQUIRES     app_parts        # 依赖 parts 组件（wireless.h 从那来）
) 