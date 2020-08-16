from scrapy import cmdline

cmdline.execute("scrapy crawl word".split())

#检测代码正确性:scrapy shell "要爬取的网站的网址"
