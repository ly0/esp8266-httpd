esp8266-httpd
=================

**Instructions in English is followed by Chinese.**



该项目编译环境为 [freertos SDK](https://github.com/espressif/esp_iot_rtos_sdk), 请将代码放在 `app/user/` 目录下  
本项目修改于 httpd, 去掉了它的 webfs, 和 httpd 区别比较大，主要区别是我去掉了我不知道干什么的东西（虽然没去完）以及让开发接口更方便，由于在修改的时候也没搞清楚 httpd 的各种参数, 所以代码比较乱, 随时更新.  
串口输出比较乱，是因为这坑爹的东西不提供动态调试，在没有弄稳定前，这些输出暂时保留。  
请 **慎重** 将其直接用于工程, 因为这是不稳定的版本。水平太次见笑了。

Compiling enviroment: [freertos SDK](https://github.com/espressif/esp_iot_rtos_sdk), pls put all files into `app/user/`  
This project is originated from *httpd*, but a little different from *httpd*, it removes webfs, CGI in httpd and other things which I can not figure out what the damn meanning. Since webfs is removed, I use `api.h` `page_xxx.c` to configure interface. It is a alpha version, not stable enough. Sorry for the uart output, since debug is so hard that I can not get to know the procedure of httpd.  
**ATTENTION: DO NOT USE IT IN YOUR BUSINESS PROJ DIRECTLY, IT IS NOT STABLE**

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



### INSTRUCTION
1. create a new file `page_xxxx.c` , template is as following
```c
#include "esp_common.h"
#include "api_struct.h"

const char* ICACHE_FLASH_ATTR
page_index(HTTPRequest *req, void *args)
{
    char *api_buffer = (char *)malloc(MAX_API_CONTENT);
    uint32_t para_amount;
    /* 
        Better do not change next statement,
        or you edit extract_params yourself
    */
    Params para[MAX_PARAM];

    /* POST OR GET */
    if(0 == req->is_post)
    {
        /* GET for getting data */
        /* This area is for GET method */
        char method[] = "GET";
        char *params = req->params;
        /* 
            using extract_params for paring parameters string
            parameters will be seperated in array para
            para_amount is the number of parameters which has already been parsed
            the maximum of parameter was define by MAX_PARAM in api_struct.h  
            E.g.
            URI: /?para1=555&para2=666, in the para

            para[0] [char *key="para1", char *value="555"]
            para[1] [char *key="para2", char *value="666"]
        */
        para_amount = extract_params(params, para);
        
    } else {
        /* POST for changing data */
        /* This area is for POST method */
        char method[] = "POST";
        /* It is the same way to parse parameters likes GET*/
        char *params = req->params;
        para_amount = extract_params(params, para);

        /* POST payload: req->post_data*/
        /*
            if the types of payload are simple key-value data,
            you also can use extract_params to parse them
        */
        para_amount = extract_params(req->post_data, para);
    }
    
    /*
        you should manipulate all output in api_buffer,
        free() will be invoked after the data was sent to client
    */
    return api_buffer;
}
```

2. Edit api.h
```c
URLRouter router_urls[] = {
    {"/", page_index},
    {"/ssid", page_ssid},
    {"/xxxx", page_xxxx} //Your new URI

};
```
add following before `#endif`
```c
extern const char* page_xxx(HTTPRequest *, void*);
```
as for why there is a *second parameter*, ahh.. this parameter is just kept for the future use.

### 演示

* `GET`: 编译以后, 浏览器访问 `http://192.168.4.1/` 和 `http://192.168.4.1/ssid`
* `POST`: 编译以后, 使用 `curl` 或者其他可以 `POST` 的工具，如 `curl` 在命令行下 `curl http://192.168.4.1/ssid?para1=A&para2=BBB --data "postpara1=a1b2&postpara2=a2b2"`

### Demo

* `GET`: After compiling, visit `http://192.168.4.1/` and `http://192.168.4.1/ssid`
* `POST`: After compiling, using `curl` or other tools that can `POST`，e.g. `curl` in CLI: `curl http://192.168.4.1/ssid?para1=A&para2=BBB --data "postpara1=a1b2&postpara2=a2b2"`

### TODOLIST
* 参考 *esphttpd* 加入一个使用 *heatshrink* 压缩的文件系统(暂定)
* 进一步精简原 *httpd* 的代码
* 加入正则引擎 (考虑使用 *slre* )

### TODOLIST
* add *heatshrink* filesystem, refered by *esphttpd* (tentative)
* simplify *httpd*'s original codes.
* add a regular expression engine (considering *slre*)

### Notes (only in chinese)
* 考虑重写cgi handler, 之前只考虑到文本数据，所以采用了 strlen 函数，这个似乎不合适
