import random
import base64
from myword.settings import USER_AGENTS
from myword.settings import PROXIES


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



