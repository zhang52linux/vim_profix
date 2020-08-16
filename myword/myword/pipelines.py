# Define your item pipelines here
#
# Don't forget to add your pipeline to the ITEM_PIPELINES setting
# See: https://docs.scrapy.org/en/latest/topics/item-pipeline.html


# useful for handling different item types with a single interface
from itemadapter import ItemAdapter
import pymysql
from pymysql import cursors
import json


class MywordPipeline:
    def __init__(self):
        self.file = open("./dest.json", 'w')

    def open_spider(self, spider):
        print("爬虫程序启动了.......")

    def process_item(self, item, spider):
        content = json.dumps(dict(item), ensure_ascii=False) #ensure_ascii=False 表示的是如果有中文就改为False
        self.file.write(content)
        return item  # 一定要返回，以便告诉引擎数据处理完了，进行下一次调用，不然信号就中断了

    def close_spider(self, spider):
        self.file.close()
        print('爬虫结束了......')
