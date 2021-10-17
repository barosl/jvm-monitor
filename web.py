#!/usr/bin/env python3

from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pathlib import Path
import json

app = FastAPI()
db = None
tpls = Jinja2Templates('tpls')

@app.get('/', response_class=HTMLResponse)
def index(req: Request):
    return tpls.TemplateResponse('index.html', {'request': req, 'db': db})

@app.on_event('startup')
def startup():
    global db
    db = json.loads(Path('db.txt').read_text())
