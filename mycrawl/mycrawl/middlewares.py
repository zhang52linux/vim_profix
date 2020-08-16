import json
import random
import base64
import redis
from mycrawl.settings import USER_AGENTS
from mycrawl.settings import PROXIES
import mycrawl.settings as setting



# 随机请求头
class RandomUserAgent(object):
    def process_request(self, request, spider):
        useragent = random.choice(USER_AGENTS)
        request.headers.setdefault("User-Agent",useragent)


# 设置代理
class RandomProxy(object):
    def process_request(self, request, spider):
        proxy = random.choice(PROXIES)
        if proxy["user_passwd"] is None:
	        request.meta["proxy"] = "http://" + proxy["ip_port"]
        else:
            base64_userpasswd = str(base64.b64encode(proxy["user_passwd"].encode("utf-8")),'utf-8')
            request.meta["proxy"] = "http://" + proxy["ip_port"]
            request.headers["Proxy-Autorization"] = "Basic " + base64_userpasswd


# 设置cookie
'''
    redis-py提供两个类Redis和StrictRedis用于实现Redis的命令，
    StrictRedis用于实现大部分官方的命令，
    并使用官方的语法和命令（比如，SET命令对应与StrictRedis.set方法）。
    Redis是StrictRedis的子类，用于向后兼容旧版本的redis-py。 
    简单说，官方推荐使用StrictRedis方法
'''
class AddCookies(object):
    def __init__(self):
        pool = redis.ConnectionPool(host=setting.REDIS_HOST, port=setting.REDIS_PORT)
        self.client = redis.StrictRedis(connection_pool=pool)


    def process_request(self, request, spider):
        # cookies = json.loads(self.client.lpop('cookies'))
        # request.cookies = cookies
        pass




