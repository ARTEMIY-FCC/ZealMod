'use strict';
/* Тексты окна. Английский — то, что уже написано в index.html; здесь только
   перевод и подстановки для строк, которые собираются в коде. */

const STRINGS = {
  en: {
    loading: 'loading…',
    programs: 'Programs', selected: 'selected',
    add_zm: '+ add .zm', all: 'all', none: 'none',
    order_tip: 'The order here is the order on the watch. Drag to rearrange.',
    look: 'Look', theme: 'Theme', menu_style: 'Menu',
    coverflow: 'covers', grid: 'grid', list: 'list', cards: 'cards',
    controls_tip: 'On the watch: ◀ ▶ move, ▲ launch, ▼ back to the timer.',
    settings: 'Buttons and settings',
    lang: 'Firmware language', lang_tip: 'The language of everything the watch shows.',
    btnmap: 'Button mapping',
    btnmap_tip: 'The timer has four buttons: two above the screen and two below. ' +
                'If the arrows are swapped, swap them here.',
    enter: 'Opening the menu',
    enter_tip: 'Hold this button for that long on the timer screen and ZealMod opens.',
    exit: 'Leaving a program', misc: 'Other',
    splash: 'splash screen on start', sound: 'sound', mute: 'mute the stock buzz',
    watch: 'Watch', backup: 'flash backup', stock: 'restore stock',
    save: 'save .gbl', flash: 'flash the timer', close: 'close',
    drop: 'drop it — we will add the .zm or .zt',
    /* динамические */
    game: 'game', app: 'app', exit_hint: 'exit', no_device: 'no timer in sight',
    is_timer: 'timer', building: 'computing…',
    usage: '{size} of {part} · {free} free · {n} programs · {pages} code pages',
    cant_build: 'does not build: {err}',
    need_esptool: 'Flashing needs esptool: pip install esptool',
    wrong_base: 'The stock firmware is not the one this mod was built against — ' +
                'it will still build, but check the watch',
    no_modules: 'No programs in the kit. Build them: cd work && make core, then zealmod mods',
    not_built: 'the image is not built yet',
    flashing: 'Flashing', backup_job: 'Flash backup', stock_job: 'Stock firmware',
    done: '{job}: done', failed: '{job}: did not work',
    installed: '«{name}» installed', theme_added: 'theme added',
    confirm_stock: 'Restore the stock firmware? ZealMod will be gone from the watch.',
    startup_failed: 'did not start: {err}',
    btn_up: '▲ up', btn_down: '▼ down', btn_left: '◀ left', btn_right: '▶ right',
    button_n: 'button {n}', seconds: '{v} s', no_programs: 'No programs',
    one_job: 'one thing at a time',
  },
  ru: {
    loading: 'загружаю…',
    programs: 'Программы', selected: 'выбрано',
    add_zm: '+ добавить .zm', all: 'все', none: 'никого',
    order_tip: 'Порядок в списке — порядок в меню часов. Перетаскивайте мышью.',
    look: 'Вид', theme: 'Тема', menu_style: 'Меню',
    coverflow: 'обложки', grid: 'сетка', list: 'список', cards: 'карточки',
    controls_tip: 'На часах: ◀ ▶ листать, ▲ запустить, ▼ выйти в таймер.',
    settings: 'Кнопки и настройки',
    lang: 'Язык прошивки', lang_tip: 'На нём будут все надписи на часах.',
    btnmap: 'Назначение кнопок',
    btnmap_tip: 'У таймера четыре кнопки: две над экраном и две под ним. ' +
                'Если стрелки перепутаны — поменяйте местами.',
    enter: 'Вход в меню',
    enter_tip: 'Держите эту кнопку столько времени на экране таймера — откроется ZealMod.',
    exit: 'Выход из программы', misc: 'Разное',
    splash: 'заставка при запуске', sound: 'звук', mute: 'глушить писк прошивки',
    watch: 'Часы', backup: 'копия флеша', stock: 'вернуть заводскую',
    save: 'сохранить .gbl', flash: 'прошить таймер', close: 'закрыть',
    drop: 'отпустите — поставим .zm или .zt',
    game: 'игра', app: 'приложение', exit_hint: 'выход', no_device: 'часов не видно',
    is_timer: 'таймер', building: 'считаю…',
    usage: '{size} из {part} · свободно {free} · программ {n} · страниц кода {pages}',
    cant_build: 'не собирается: {err}',
    need_esptool: 'Для заливки нужен esptool: pip install esptool',
    wrong_base: 'Стоковая прошивка не та, под которую собран мод — соберётся, ' +
                'но проверьте часы',
    no_modules: 'В комплекте нет программ. Соберите их: cd work && make core, затем zealmod mods',
    not_built: 'образ ещё не собран',
    flashing: 'Заливка', backup_job: 'Копия флеша', stock_job: 'Заводская прошивка',
    done: '{job}: готово', failed: '{job}: не получилось',
    installed: '«{name}» поставлена', theme_added: 'тема добавлена',
    confirm_stock: 'Вернуть заводскую прошивку? ZealMod с часов пропадёт.',
    startup_failed: 'не завёлся: {err}',
    btn_up: '▲ вверх', btn_down: '▼ вниз', btn_left: '◀ влево', btn_right: '▶ вправо',
    button_n: 'кнопка {n}', seconds: '{v} с', no_programs: 'Модулей нет',
    one_job: 'одно дело за раз',
  },
};

let UILANG = (() => {
  try { return localStorage.getItem('zealmod.lang') || 'en'; } catch (e) { return 'en'; }
})();

function t(key, vars) {
  const s = (STRINGS[UILANG] || STRINGS.en)[key] || STRINGS.en[key] || key;
  return vars ? s.replace(/\{(\w+)\}/g, (_, k) => (vars[k] == null ? '' : vars[k])) : s;
}

function applyLang(lang) {
  UILANG = STRINGS[lang] ? lang : 'en';
  try { localStorage.setItem('zealmod.lang', UILANG); } catch (e) { /* и ладно */ }
  document.documentElement.lang = UILANG;
  document.querySelectorAll('[data-i18n]').forEach((el) => {
    el.textContent = t(el.dataset.i18n);
  });
}
