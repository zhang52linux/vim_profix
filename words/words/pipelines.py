# -*- coding: utf-8 -*-

# Define your item pipelines here
#
# Don't forget to add your pipeline to the ITEM_PIPELINES setting
# See: https://docs.scrapy.org/en/latest/topics/item-pipeline.html
import pymysql
from twisted.enterprise import adbapi
from pymysql import cursors

class WordsPipeline(object):
    def __init__(self):
        dbparams = {
            'host':'localhost',
            'user':'root',
            'password':'linux250',
            'port':3306,
            'database':'dictory'
        }
        self.conn = pymysql.connect(**dbparams) #两个星表示将数据化为key和value
        self.cursor = self.conn.cursor()

    def search_db(self, name):
        try:
            sql = "SELECT  * FROM  words  WHERE  name = '{}'".format(name)
            self.cursor.execute(sql)
            res = self.cursor.fetchall()
            return len(res)
        except Exception as e:
            return 'error'

    def open_spider(self,spider):
        try:
            sql = "CREATE TABLE IF NOT EXISTS words(name VARCHAR(100) primary key,bihua VARCHAR(100),pinyin VARCHAR(100),bushou VARCHAR(100), jiben TEXT,xiangxi1 TEXT,xiangxi2 TEXT, xiangxi3 TEXT,hanyu TEXT);"
            self.cursor.execute(sql)
        except Exception as e:
            print(e)
        print('爬虫开始了......')

    def process_item(self, item, spider):
        try:
            sql = "INSERT INTO words VALUES('%s','%s','%s','%s','%s','%s','%s','%s','%s');"%(item['name'],item['bihua'],item['pinyin'],item['bushou'],item['jiben'],item['xiangxi1'],item['xiangxi2'],item['xiangxi3'],item['hanyu'])
            self.cursor.execute(sql)
        except Exception as e:
            print('插入信息出错:{}'.format(e))
        return item

    def close_spider(self,spider):
        self.conn.close()
        print('爬虫结束了......')


class WordsPipelineDbAPI(object):
    def __init__(self):
        dbparams = {
            'host': 'localhost',
            'user': 'root',
            'password': 'linux250',
            'port': 3306,
            'database': 'dictory',
            'cursorclass':cursors.DictCursor
        }
        self.dbpool = adbapi.ConnectionPool('pymysql',**dbparams)


    def process_item(self, item, spider):
        defer = self.dbpool.runInteraction(self.insert_item,item)
        defer.addErrback(self.handle_error,item)

    def insert_item(self,cursor,item):
        sql = "INSERT INTO words VALUES('%s','%s','%s','%s','%s','%s','%s','%s','%s');" % (
            item['name'], item['bihua'], item['pinyin'], item['bushou'], item['jiben'], item['xiangxi1'],
            item['xiangxi2'], item['xiangxi3'], item['hanyu'])
        cursor.execute(sql)

    def handle_error(self,error,item):
        print('=' * 10 + "error" + '=' * 10)
        print(error)
        print('=' * 10 + "error" + '=' * 10)
