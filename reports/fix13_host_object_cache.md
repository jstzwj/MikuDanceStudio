# Fix13：宿主对象卸载后的悬空身份索引

## 确认的问题及触发范围

`third_party/mmeffect/src/mme_host.cpp` 的 `MmeHostBeginScene` 原先从
`objectList` 删除已消失对象并释放 `MmhCachedObject`，但没有从
`objectById` 删除对应项。之后的新对象检测以 `objectById.find(id)`
判断是否已经登记；宿主 ID 来自模型或附件指针，分配器可复用该地址。

因此，在一个对象已经被一帧清理后，加载对象若复用同一地址，会跳过
`OnCreateModel`，并在重建 `objectByOrder` 时取回已释放的缓存指针。
这是可直接从代码确认的生命周期错误，可能导致效果不登记或释放后访问。
**这不能证明首次加载 RayMMD 就发生的黑屏由此引起。** 同一帧之前就完成
卸载及同地址重载、且未观察到中间缺失状态的情况，也不由本修复解决。

## 修复

新增 `mmeffect/host_object_cache.h` 的 `RemoveMissingHostObjects`，让
生产帧清理流程在释放对象之前移除身份索引，再执行原有删除通知、纹理引用
释放、材质数据移除及对象释放。存活对象的顺序和记录保持不变；绘制顺序图
仍按后续原有流程重建。保留工作区已经存在的初始化失败处理修改。

## 验证

`tests/mme_host_object_cache_test.cpp` 直接调用生产辅助函数，覆盖：卸载后
两个容器同步移除；释放前身份不可查询；存活对象不被替换；重复帧不二次
释放；相同宿主 ID 重新加载必须重新登记；全部卸载后容器清空。

使用 MSVC x64 `/std:c++17 /EHsc /W4` 独立编译及运行通过，输出
`MME host object cache regression passed`。完整工程构建及 CTest 接入由主线
执行。本测试没有使用真实设备或声称完成 RayMMD 图像 A/B 验证。
