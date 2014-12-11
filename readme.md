esp8266-uhttpd
=================

本项目修改于 httpd, 去掉了它的 webfs, 由于在修改的时候也没搞清楚 httpd 的各种参数, 所以代码比较乱, 随时更新.

串口输出比较乱，是因为这坑爹的东西不提供动态调试，在没有弄稳定前，这些输出暂时保留。

请 **慎重** 将其直接用于工程, 因为这是不稳定的版本。水平太次见笑了。


### 使用方法
1. 建立一个新文件 `page_xxxx.c` ，内容模板和介绍如下，有些地方可以适当修改
```c
#include "esp_common.h"
#include "api_struct.h"

const char* ICACHE_FLASH_ATTR
page_index(HTTPRequest *req, void *args)
{
    /* 建议只修改 GET 和 POST 方法的部分，其他部分最好不要动 */
    /* 注意该处的内存将会在发送完成后调用 free */
    char *api_buffer = (char *)malloc(MAX_API_CONTENT);
    uint32_t para_amount;
    /* 最好这么写，建议不要修改下面这句 */
    Params para[MAX_PARAM];

    /* POST OR GET */
    if(0 == req->is_post)
    {
        /* 根据RESTful原则, GET一般用于获取数据 */
        /* 这里是 GET 方法的区域 */
        char method[] = "GET";
        char *params = req->params;
        /* 
            下面这个  extract_params 调用来解析参数
            参数内容会存到 para 数组里
            其中 para_amount 是解析了的参数值
            最大解析参数数目由 api_struct.h 里的宏 MAX_PARAM 确定
            如 /?para1=555&para2=666 则 para 里的结构是

            para[0] [char *key="para1", char *value="555"]
            para[1] [char *key="para2", char *value="666"]
        */
        para_amount = extract_params(params, para);
        
    } else {
        /* 根据RESTful原则, POST一般用于修改数据 */
        /* 这里是 POST 方法的区域 */
        char method[] = "POST";
        /* 解析 parameters 的方法和 GET 相同*/
        char *params = req->params;
        para_amount = extract_params(params, para);

        /* POST 的载荷(payload)可以通过 req->post_data 获得*/
        /* 同理，如果 payload 类型是简单的key-value型数据，可以一样采用 extract_params 函数*/
        para_amount = extract_params(req->post_data, para);
    }
    
    /* 返回内容全部在 api_buffer 操作，他会在传输完成后调用 free */
    return api_buffer;
}
```

2. 修改 api.h
```c
URLRouter router_urls[] = {
    {"/", page_index},
    {"/ssid", page_ssid},
    {"/xxxx", page_xxxx} //你的入口加在这里

};
```
并且在 `#endif` 前加入
```c
extern const char* page_xxx(HTTPRequest *, void*);
```
至于为什么要有第二个参数，是因为方便以后可能传参进去。