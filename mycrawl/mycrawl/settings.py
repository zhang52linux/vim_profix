# Scrapy settings for mycrawl project
#
# For simplicity, this file contains only settings considered important or
# commonly used. You can find more settings consulting the documentation:
#
#     https://docs.scrapy.org/en/latest/topics/settings.html
#     https://docs.scrapy.org/en/latest/topics/downloader-middleware.html
#     https://docs.scrapy.org/en/latest/topics/spider-middleware.html

BOT_NAME = 'mycrawl'

SPIDER_MODULES = ['mycrawl.spiders']
NEWSPIDER_MODULE = 'mycrawl.spiders'

# 保存信日志信息
# LOG_FILE = "mycrawl.log"
# 定义日志信息的等级，保存高于或等于此日志等级的信息
# LOG_LEVEL = 'INFO'
JOBDIR='zant'

# Crawl responsibly by identifying yourself (and your website) on the user-agent
#USER_AGENT = 'mycrawl (+http://www.yourdomain.com)'

# Obey robots.txt rules
ROBOTSTXT_OBEY = False

# Configure maximum concurrent requests performed by Scrapy (default: 16)
#CONCURRENT_REQUESTS = 32

# Configure a delay for requests for the same website (default: 0)
# See https://docs.scrapy.org/en/latest/topics/settings.html#download-delay
# See also autothrottle settings and docs
#DOWNLOAD_DELAY = 3
# The download delay setting will honor only one of:
#CONCURRENT_REQUESTS_PER_DOMAIN = 16
#CONCURRENT_REQUESTS_PER_IP = 16

# Disable cookies (enabled by default)
COOKIES_ENABLED = True

# Disable Telnet Console (enabled by default)
#TELNETCONSOLE_ENABLED = False

# Override the default request headers:
DEFAULT_REQUEST_HEADERS = {
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.61 Safari/537.36',
}

# Enable or disable spider middlewares
# See https://docs.scrapy.org/en/latest/topics/spider-middleware.html
#SPIDER_MIDDLEWARES = {
#    'mycrawl.middlewares.MycrawlSpiderMiddleware': 543,
#}

# Enable or disable downloader middlewares
# See https://docs.scrapy.org/en/latest/topics/downloader-middleware.html
DOWNLOADER_MIDDLEWARES = {
    'mycrawl.middlewares.RandomUserAgent': 100,
    # 'mycrawl.middlewares.RandomProxy': 200,
    'mycrawl.middlewares.AddCookies': 300,
}

USER_AGENTS = [
    "Mozilla/5.0 (Windows NT 6.1; rv:2.0.1) Gecko/20100101 Firefox/4.0.1",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_7_0) AppleWebKit/535.11 (KHTML, like Gecko) Chrome/17.0.963.56 Safari/535.11",
    "Mozilla/5.0 (Linux; U; android 2.3.7; en-us; Nexus One Build/FRF91) AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile Safari/533.1",
]


PROXIES = [
    {"ip_port": "120.26.39.197:16818","user_passwd": "1757846498:tfvsavfr"},  # 付费代理，需要密码验证的，稳定
    # {"ip_port": "125.108.123.95:9000", "user_passwd": ""},  # 免费代理，不用密码验证的,容易出错
]


# Enable or disable extensions
# See https://docs.scrapy.org/en/latest/topics/extensions.html
#EXTENSIONS = {
#    'scrapy.extensions.telnet.TelnetConsole': None,
#}

# Configure item pipelines
# See https://docs.scrapy.org/en/latest/topics/item-pipeline.html
# 如果有redis数据库，这个可以注释掉，免得在本地也生成一份资料
# ITEM_PIPELINES = {
#     'mycrawl.pipelines.MycrawlPipeline': 300,
# }

#链接mongodb数据库
MONGODB_HOST = "127.0.0.1"
MONGODB_POST = 27017
MONGODB_DBNAME = "crawl"
MONGODB_SHEETNAME = "douban"

# Enable and configure the AutoThrottle extension (disabled by default)
# See https://docs.scrapy.org/en/latest/topics/autothrottle.html
#AUTOTHROTTLE_ENABLED = True
# The initial download delay
#AUTOTHROTTLE_START_DELAY = 5
# The maximum download delay to be set in case of high latencies
#AUTOTHROTTLE_MAX_DELAY = 60
# The average number of requests Scrapy should be sending in parallel to
# each remote server
#AUTOTHROTTLE_TARGET_CONCURRENCY = 1.0
# Enable showing throttling stats for every response received:
#AUTOTHROTTLE_DEBUG = False

# Enable and configure HTTP caching (disabled by default)
# See https://docs.scrapy.org/en/latest/topics/downloader-middleware.html#httpcache-middleware-settings
#HTTPCACHE_ENABLED = True
#HTTPCACHE_EXPIRATION_SECS = 0
#HTTPCACHE_DIR = 'httpcache'
#HTTPCACHE_IGNORE_HTTP_CODES = []
#HTTPCACHE_STORAGE = 'scrapy.extensions.httpcache.FilesystemCacheStorage'



#scrapy_redis项目配置:
# 重新配置各模块
# 使用scrapy-redis的去重组件，不使用scrapy默认的
DUPEFILTER_CLASS = "scrapy_redis.dupefilter.RFPDupeFilter"
# 使用scrapy-redis的调度器组件，不使用scrapy默认的
SCHEDULER = "scrapy_redis.scheduler.Scheduler"
# 使用scrapy-redis的管道文件，不使用scrapy默认的
ITEM_PIPELINES = {
    'scrapy_redis.pipelines.RedisPipeline': 300,
}
# 是否保持调度器队列，断点续爬
SCHEDULER_PERSIST = True
# 配置redis数据库链接地址
REDIS_HOST = '127.0.0.1'
REDIS_PORT = 6379
