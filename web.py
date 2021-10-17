#!/usr/bin/env python3

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pathlib import Path
import json
from markupsafe import Markup
import datetime

app = FastAPI()
db = None
tpls = Jinja2Templates('tpls')

def emp_last_word(text):
    try:
        pos = text.rindex('.') + 1
    except ValueError:
        return text
    else:
        return text[:pos] + Markup('<b>') + text[pos:] + Markup('</b>')

tpls.env.filters['emp_last_word'] = emp_last_word

@app.get('/', response_class=HTMLResponse)
def index(req: Request):
    startup()
    return tpls.TemplateResponse('index.html', {'request': req, 'db': db})

@app.on_event('startup')
def startup():
    global db
    db = json.loads(Path('db.txt').read_text())
    db = [{**x, 'ts_usec': datetime.datetime.utcfromtimestamp(x['ts_usec'] / 1e6)} for x in db]
