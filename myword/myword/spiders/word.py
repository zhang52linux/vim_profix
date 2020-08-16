import scrapy
from myword.items import MywordItem




class WordSpider(scrapy.Spider):
    name = 'word'
    allowed_domains = ['tencent.com']
    # 只要是可迭代的对象就可以
    start_urls = ['https://careers.tencent.com/search.html']

    def parse(self, response):
        node_list = response.xpath("//div[@class='li_txt']")
        for node in node_list:
            name = node.xpath("./h3/text()").extract()
            title = node.xpath("./h4/text()").extract()
            info = node.xpath("./p/text()").extract()
            item = MywordItem()
            item['name'] = name[0]
            item['title'] = title[0]
            item['info'] = info[0]
            yield item
            yield scrapy.Request("https://www.baidu.com/",callback=self.parse)
            yield scrapy.Request("https://blog.csdn.net/", callback=self.parse)




